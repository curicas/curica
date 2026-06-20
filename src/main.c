/**
 * @file main.c
 * @brief Curica Environment OS Kernel Entry Point.
 *
 * Implements component logic for the Curica Environment OS Kernel.
 * Curica is a secure microkernel OS that employs a strict POSIX Virtual File System (VFS)
 * with /bin, /home/user, and pseudo-filesystems (/dev, /proc). It uses JS natively as the
 * systems shell scripting language to pipe I/O and spawn WASM processes, enforcing
 * capability-based security (allow_run, allow_net, allow_read, allow_write, allow_ffi).
 * Furthermore, the kernel freezes environments into Actually Portable Executables (APEs)
 * and features Source Compilation Fallback, Virtual Networking Mocking, and
 * Foreign Sandbox IPC attached.
 */
#define _GNU_SOURCE
#include "alloc.h"
#include "bytecode.h"
#include "compiler.h"
#include "event_loop.h"
#include "value.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#ifdef __COSMOPOLITAN__
int pledge(const char *promises, const char *execpromises);
#endif
#include "formatter.h"
#include "repl.h"
#include "ts_stripper.h"
#include "bootstrapper.h"

// ZIP Format Signatures
#define SIGNATURE_LOCAL_FILE_HEADER 0x04034b50
#define SIGNATURE_CENTRAL_DIRECTORY 0x02014b50
#define SIGNATURE_EOCD 0x06054b50

// CRC-32 calculation for ZIP directory entry validation
// Extract the JS bytecode from the embedded ZIP payload inside the APE
// executable
static uint8_t *extract_bundled_bytecode(uint32_t *out_size) {
  FILE *f = fopen("/zip/main.curi", "rb");
  if (!f)
    return NULL;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  uint8_t *buf = malloc(size);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  size_t read_bytes = fread(buf, 1, size, f);
  fclose(f);

  if (read_bytes != (size_t)size) {
    free(buf);
    return NULL;
  }

  *out_size = size;
  return buf;
}

static uint8_t *read_binary_file(const char *path, uint32_t *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  uint8_t *buf = malloc(size);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  size_t read_bytes = fread(buf, 1, size, f);
  fclose(f);

  if (read_bytes != (size_t)size) {
    free(buf);
    return NULL;
  }

  *out_size = size;
  return buf;
}

// Utility to read entire text file
static char *read_entire_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(size + 1);
  size_t read_bytes = fread(buf, 1, size, f);
  buf[read_bytes] = '\0';
  fclose(f);
  return buf;
}

#include <cosmo.h>

// Helper to determine executable path
static void get_exec_path(char *out_path, size_t size, const char *argv0) {
  (void)argv0;
  const char *path = GetProgramExecutableName();
  if (path) {
    strncpy(out_path, path, size - 1);
    out_path[size - 1] = '\0';
  } else {
    out_path[0] = '\0';
  }
}

/**
 * @brief Main Entry Point for the Curica Environment OS Kernel (APE)
 *
 * This function handles the initialization of the VM and execution of scripts.
 * Curica is compiled via Cosmopolitan libc as an Actually Portable Executable
 * (APE), meaning this exact binary runs natively on Linux, macOS, and Windows.
 *
 * Key Architectural Boot Sequences:
 * 1. **VFS Bootstrapping**: Initializes the Virtual File System, enforcing POSIX FHS 
 *    compliance, pseudo-filesystems (/dev, /proc), and mounting `--attach` overlays.
 * 2. **Signal Handlers**: Hooks `SIGSEGV` to catch stack overflows natively.
 * 3. **Immutable APE Unpacking**: Checks if the binary contains a frozen VFS payload 
 *    created via `curica build`. If found, it deserializes the environment instantly.
 * 4. **Execution Modes**: Routes to the Curica OS Shell REPL, AOT Compiler, 
 *    or direct kernel execution.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return int Exit status code
 */
int main(int argc, char **argv) {
  char exec_path[1024];
  get_exec_path(exec_path, sizeof(exec_path), argv[0]);

  // Check if executable contains a bundled zip payload
  uint32_t cbc_size = 0;
  uint8_t *cbc_data = extract_bundled_bytecode(&cbc_size);

  if (cbc_data != NULL) {
    // Run in bundled executable mode
    arena_init(32); // 32MB Memory Arena

    extern void vfs_init(void);
    vfs_init();

    CompiledProgram *prog = deserialize_program(cbc_data, cbc_size);
    if (!prog) {
      fprintf(stderr,
              "Fatal Error: Failed to deserialize bundled CURI bytecode.\n");
      free(cbc_data);
      arena_free();
      return 1;
    }

    EventLoop loop;
    el_init(&loop);
    VM vm;
    vm_init(&vm);
    vm.functions = prog->functions;
    vm.function_count = prog->function_count;

    char* boot_entry = bootstrap_environment(&vm, "/zip/curica.env.json");
    if (!boot_entry) {
      // Grant permissions by default to legacy bundled executables without config
      vm.allow_read = true;
      vm.allow_write = true;
      vm.allow_net = true;
      vm.allow_ffi = true;
      vm.allow_run = true;
    } else {
      free(boot_entry); // Entrypoint ignored for bundled bytecodes
    }

    vm_load_program(&vm, prog);

    // Expose require globally for bundled execution
    extern Value js_process_require(VM * vm, Value this_val, int arg_count,
                                    Value *args);
    Value require_str = create_string("require", 7);
    Value require_fn =
        create_native_function((void *)js_process_require, require_str);
    object_set(vm.global_obj, require_str, require_fn);
    // Removed pledge for N-API testing
    extern unsigned char src_js_fetch_js[];
    extern unsigned int src_js_fetch_js_len;
    char *fetch_src = malloc(src_js_fetch_js_len + 1);
    memcpy(fetch_src, src_js_fetch_js, src_js_fetch_js_len);
    fetch_src[src_js_fetch_js_len] = '\0';
    CompiledProgram *fetch_prog = compile_source(fetch_src);
    free(fetch_src);
    if (fetch_prog) {
      vm_load_program(&vm, fetch_prog);
      vm_run(&vm);
    }

    extern unsigned char src_js_package_manager_js[];
    extern unsigned int src_js_package_manager_js_len;
    char *pkg_src = malloc(src_js_package_manager_js_len + 1);
    memcpy(pkg_src, src_js_package_manager_js, src_js_package_manager_js_len);
    pkg_src[src_js_package_manager_js_len] = '\0';
    CompiledProgram *pkg_prog = compile_source(pkg_src);
    free(pkg_src);
    if (pkg_prog) {
      vm_load_program(&vm, pkg_prog);
      vm_run(&vm);
      vm_drain_next_tick(&vm);
      vm_drain_microtasks(&vm);
      el_run(&loop);
    }

    vm_load_program(&vm, prog);
    vm_run(&vm);
    vm_drain_next_tick(&vm);
    vm_drain_microtasks(&vm);
    el_run(&loop);

    vm_free(&vm);
    free_compiled_program(prog);
    free(cbc_data);
    arena_free();
    return 0;
  }

  // If no bundled bytecode is found, standard CLI mode is active
  if (argc < 2) {
    // No arguments: Enter interactive REPL / OS Shell
    repl_start();
    return 0;
  }

  const char *command = argv[1];

  if (strcmp(command, "run") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: curica run [flags] <file.js>\n");
      return 1;
    }
    arena_init(32);

    EventLoop loop;
    el_init(&loop);
    VM vm;
    vm_init(&vm);

    int arg_idx = 2;
    while (arg_idx < argc && strncmp(argv[arg_idx], "--", 2) == 0) {
      if (strcmp(argv[arg_idx], "--allow-all") == 0) {
        vm.allow_net = true;
        vm.allow_read = true;
        vm.allow_write = true;
        vm.allow_run = true;
        vm.allow_ffi = true;
      } else if (strcmp(argv[arg_idx], "--allow-net") == 0) {
        vm.allow_net = true;
      } else if (strcmp(argv[arg_idx], "--allow-read") == 0) {
        vm.allow_read = true;
      } else if (strcmp(argv[arg_idx], "--allow-write") == 0) {
        vm.allow_write = true;
      } else if (strcmp(argv[arg_idx], "--allow-run") == 0) {
        vm.allow_run = true;
      } else if (strcmp(argv[arg_idx], "--allow-ffi") == 0) {
        vm.allow_ffi = true;
      } else if (strncmp(argv[arg_idx], "--attach=", 9) == 0) {
        const char* attach_path = argv[arg_idx] + 9;
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_un un_addr = {0};
            un_addr.sun_family = AF_UNIX;
            strncpy(un_addr.sun_path, attach_path, sizeof(un_addr.sun_path) - 1);
            if (connect(fd, (struct sockaddr*)&un_addr, sizeof(un_addr)) == 0) {
                int flags = fcntl(fd, F_GETFL, 0);
                fcntl(fd, F_SETFL, flags | O_NONBLOCK);
                vm.ipc_fd = fd;
                extern Value js_make_socket_object(VM* vm, int fd);
                Value process_str = create_string("process", 7);
                Value process_obj = object_get(vm.global_obj, process_str);
                object_set(process_obj, create_string("ipcSocket", 9), js_make_socket_object(&vm, fd));
            } else {
                fprintf(stderr, "Failed to attach to IPC socket: %s\n", attach_path);
                close(fd);
                return 1;
            }
        }
      } else {
        fprintf(stderr, "Unknown flag: %s\n", argv[arg_idx]);
        return 1;
      }
      arg_idx++;
    }

    if (arg_idx >= argc) {
      fprintf(
          stderr,
          "Error: Missing target file.\nUsage: curica run [flags] <file.js>\n");
      return 1;
    }

    extern unsigned char src_js_fetch_js[];
    extern unsigned int src_js_fetch_js_len;
    char *fetch_src = malloc(src_js_fetch_js_len + 1);
    memcpy(fetch_src, src_js_fetch_js, src_js_fetch_js_len);
    fetch_src[src_js_fetch_js_len] = '\0';
    CompiledProgram *fetch_prog = compile_source(fetch_src);
    free(fetch_src);
    if (fetch_prog) {
      vm_load_program(&vm, fetch_prog);
      vm_run(&vm);
    }

    extern Value js_process_require(VM * vm, Value this_val, int arg_count,
                                    Value *args);
    char resolved_main_path[1024];
    if (argv[arg_idx][0] == '/' ||
        (argv[arg_idx][0] == '.' &&
         (argv[arg_idx][1] == '/' ||
          (argv[arg_idx][1] == '.' && argv[arg_idx][2] == '/')))) {
      strncpy(resolved_main_path, argv[arg_idx],
              sizeof(resolved_main_path) - 1);
      resolved_main_path[sizeof(resolved_main_path) - 1] = '\0';
    } else {
      snprintf(resolved_main_path, sizeof(resolved_main_path), "./%s",
               argv[arg_idx]);
    }
    Value path_val =
        create_string(resolved_main_path, strlen(resolved_main_path));
    Value dirname_val = create_string(".", 1);
    Value req_args[2] = {path_val, dirname_val};
    js_process_require(&vm, VAL_UNDEFINED, 2, req_args);

    vm_drain_next_tick(&vm);
    vm_drain_microtasks(&vm);
    el_run(&loop);

    vm_free(&vm);
    arena_free();
    return 0;
  } else if (strcmp(command, "boot") == 0) {
    const char *env_file = "curica.env.json";
    if (argc >= 3) {
      env_file = argv[2];
    }
    arena_init(32);
    EventLoop loop;
    el_init(&loop);
    VM vm;
    vm_init(&vm);

    char *entry_path = bootstrap_environment(&vm, env_file);
    if (!entry_path) {
      vm_free(&vm);
      arena_free();
      return 1;
    }

    extern unsigned char src_js_fetch_js[];
    extern unsigned int src_js_fetch_js_len;
    char *fetch_src = malloc(src_js_fetch_js_len + 1);
    memcpy(fetch_src, src_js_fetch_js, src_js_fetch_js_len);
    fetch_src[src_js_fetch_js_len] = '\0';
    CompiledProgram *fetch_prog = compile_source(fetch_src);
    free(fetch_src);
    if (fetch_prog) {
      vm_load_program(&vm, fetch_prog);
      vm_run(&vm);
    }

    extern Value js_process_require(VM * vm, Value this_val, int arg_count,
                                    Value *args);

    // Expose require globally for package manager
    Value require_str = create_string("require", 7);
    Value require_fn = create_native_function((void *)js_process_require, require_str);
    object_set(vm.global_obj, require_str, require_fn);

    extern unsigned char src_js_package_manager_js[];
    extern unsigned int src_js_package_manager_js_len;
    char *pkg_src = malloc(src_js_package_manager_js_len + 1);
    memcpy(pkg_src, src_js_package_manager_js, src_js_package_manager_js_len);
    pkg_src[src_js_package_manager_js_len] = '\0';
    CompiledProgram *pkg_prog = compile_source(pkg_src);
    free(pkg_src);
    if (pkg_prog) {
      vm_load_program(&vm, pkg_prog);
      vm_run(&vm);
      vm_drain_next_tick(&vm);
      vm_drain_microtasks(&vm);
      el_run(&loop);
    }
    char resolved_main_path[1024];
    if (entry_path[0] == '/' ||
        (entry_path[0] == '.' &&
         (entry_path[1] == '/' ||
          (entry_path[1] == '.' && entry_path[2] == '/')))) {
      strncpy(resolved_main_path, entry_path,
              sizeof(resolved_main_path) - 1);
      resolved_main_path[sizeof(resolved_main_path) - 1] = '\0';
    } else {
      snprintf(resolved_main_path, sizeof(resolved_main_path), "./%s",
               entry_path);
    }
    Value path_val =
        create_string(resolved_main_path, strlen(resolved_main_path));
    Value dirname_val = create_string(".", 1);
    Value req_args[2] = {path_val, dirname_val};
    js_process_require(&vm, VAL_UNDEFINED, 2, req_args);

    vm_drain_next_tick(&vm);
    vm_drain_microtasks(&vm);
    el_run(&loop);

    vm_free(&vm);
    arena_free();
    free(entry_path);
    if (fetch_prog) free_compiled_program(fetch_prog);
    return 0;
  } else if (strcmp(command, "exec") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: curica exec <file.curi>\n");
      return 1;
    }

    uint32_t cbc_size = 0;
    uint8_t *cbc_data = read_binary_file(argv[2], &cbc_size);
    if (!cbc_data) {
      fprintf(stderr, "Error: Failed to read binary file '%s'\n", argv[2]);
      return 1;
    }

    CompiledProgram *prog = deserialize_program(cbc_data, cbc_size);
    if (!prog) {
      fprintf(stderr, "Error: Failed to deserialize bytecode from '%s'\n",
              argv[2]);
      free(cbc_data);
      return 1;
    }

    arena_init(32);
    EventLoop loop;
    el_init(&loop);
    VM vm;
    vm_init(&vm);
    vm.functions = prog->functions;
    vm.function_count = prog->function_count;

    // Grant permissions for exec by default
    vm.allow_read = true;
    vm.allow_write = true;
    vm.allow_net = true;
    vm.allow_ffi = true;
    vm.allow_run = true;

    vm_load_program(&vm, prog);

    // Expose require globally
    extern Value js_process_require(VM * vm, Value this_val, int arg_count,
                                    Value *args);
    Value require_str = create_string("require", 7);
    Value require_fn =
        create_native_function((void *)js_process_require, require_str);
    object_set(vm.global_obj, require_str, require_fn);

    extern unsigned char src_js_fetch_js[];
    extern unsigned int src_js_fetch_js_len;
    char *fetch_src = malloc(src_js_fetch_js_len + 1);
    memcpy(fetch_src, src_js_fetch_js, src_js_fetch_js_len);
    fetch_src[src_js_fetch_js_len] = '\0';
    CompiledProgram *fetch_prog = compile_source(fetch_src);
    free(fetch_src);
    if (fetch_prog) {
      vm_load_program(&vm, fetch_prog);
      vm_run(&vm);
    }

    vm_load_program(&vm, prog);
    vm_run(&vm);
    vm_drain_next_tick(&vm);
    vm_drain_microtasks(&vm);
    el_run(&loop);

    vm_free(&vm);
    free_compiled_program(prog);
    free(cbc_data);
    arena_free();
    return 0;
  } else if (strcmp(command, "test") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: curica test <directory>\n");
      return 1;
    }
    arena_init(32);
    EventLoop loop;
    el_init(&loop);
    VM vm;
    vm_init(&vm);

    // Give test runner full permissions
    vm.allow_net = true;
    vm.allow_read = true;
    vm.allow_write = true;
    vm.allow_run = true;
    vm.allow_ffi = true;

    // Expose require globally for test_runner.js
    extern Value js_process_require(VM * vm, Value this_val, int arg_count,
                                    Value *args);
    Value require_str = create_string("require", 7);
    Value require_fn =
        create_native_function((void *)js_process_require, require_str);
    object_set(vm.global_obj, require_str, require_fn);

    extern unsigned char src_js_fetch_js[];
    extern unsigned int src_js_fetch_js_len;
    char *fetch_src = malloc(src_js_fetch_js_len + 1);
    memcpy(fetch_src, src_js_fetch_js, src_js_fetch_js_len);
    fetch_src[src_js_fetch_js_len] = '\0';
    CompiledProgram *fetch_prog = compile_source(fetch_src);
    free(fetch_src);
    if (fetch_prog) {
      vm_load_program(&vm, fetch_prog);
      vm_run(&vm);
    }

    extern unsigned char src_js_test_runner_js[];
    extern unsigned int src_js_test_runner_js_len;
    char *runner_src = malloc(src_js_test_runner_js_len + 1);
    memcpy(runner_src, src_js_test_runner_js, src_js_test_runner_js_len);
    runner_src[src_js_test_runner_js_len] = '\0';
    CompiledProgram *runner_prog = compile_source(runner_src);
    free(runner_src);
    if (runner_prog) {
      vm_load_program(&vm, runner_prog);
      vm_run(&vm);
    }

    Value run_tests_key = create_string("__run_tests", 11);
    Value run_tests_func = object_get(vm.global_obj, run_tests_key);
    if (IS_POINTER(run_tests_func)) {
      Value dir_val = create_string(argv[2], strlen(argv[2]));
      Value args[1] = {dir_val};
      extern Value vm_call_function(VM * vm, Value func_val, int arg_count,
                                    Value *args);
      vm_call_function(&vm, run_tests_func, 1, args);
    }

    vm_drain_next_tick(&vm);
    vm_drain_microtasks(&vm);
    el_run(&loop);

    vm_free(&vm);
    if (fetch_prog)
      free_compiled_program(fetch_prog);
    if (runner_prog)
      free_compiled_program(runner_prog);
    arena_free();
    return 0;
  } else if (strcmp(command, "fmt") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: curica fmt <file.js>\n");
      return 1;
    }
    format_javascript_file(argv[2]);
    return 0;
  } else if (strcmp(command, "compile") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: curica compile <in.js> <out.curi>\n");
      return 1;
    }
    char *src = read_entire_file(argv[2]);
    if (!src) {
      fprintf(stderr, "Error: Failed to read source file '%s'\n", argv[2]);
      return 1;
    }

    int path_len = strlen(argv[2]);
    if ((path_len > 3 && strcmp(argv[2] + path_len - 3, ".ts") == 0) ||
        (path_len > 4 && strcmp(argv[2] + path_len - 4, ".mts") == 0)) {
      strip_typescript_types(src);
    }

    CompiledProgram *prog = compile_source(src);
    if (!prog) {
      fprintf(stderr, "Compilation failed.\n");
      free(src);
      return 1;
    }

    uint32_t serialized_size = 0;
    uint8_t *serialized_data = serialize_program(prog, &serialized_size);

    FILE *out = fopen(argv[3], "wb");
    if (!out) {
      fprintf(stderr, "Error: Failed to open output file '%s'\n", argv[3]);
      free(serialized_data);
      free_compiled_program(prog);
      free(src);
      return 1;
    }
    fwrite(serialized_data, 1, serialized_size, out);
    fclose(out);

    free(serialized_data);
    free_compiled_program(prog);
    free(src);
    printf("Successfully compiled to '%s' (%u bytes)\n", argv[3],
           serialized_size);
    return 0;
  } else if (strcmp(command, "build") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: curica build <in.js> <out>\n");
      return 1;
    }
    int path_len = strlen(argv[2]);
    uint32_t serialized_size = 0;
    uint8_t *serialized_data = NULL;
    CompiledProgram *prog = NULL;
    char *src = NULL;

    if (path_len > 5 && strcmp(argv[2] + path_len - 5, ".curi") == 0) {
      serialized_data = read_binary_file(argv[2], &serialized_size);
      if (!serialized_data) {
        fprintf(stderr, "Error: Failed to read binary file '%s'\n", argv[2]);
        return 1;
      }
    } else {
      src = read_entire_file(argv[2]);
      if (!src) {
        fprintf(stderr, "Error: Failed to read source file '%s'\n", argv[2]);
        return 1;
      }

      if ((path_len > 3 && strcmp(argv[2] + path_len - 3, ".ts") == 0) ||
          (path_len > 4 && strcmp(argv[2] + path_len - 4, ".mts") == 0)) {
        strip_typescript_types(src);
      }

      prog = compile_source(src);
      if (!prog) {
        fprintf(stderr, "Compilation failed.\n");
        free(src);
        return 1;
      }

      serialized_data = serialize_program(prog, &serialized_size);
    }

    // 1. Copy the current running compiler executable driver to out
    FILE *src_exec = fopen(exec_path, "rb");
    if (!src_exec) {
      fprintf(stderr,
              "Error: Failed to read compiler driver executable at '%s'\n",
              exec_path);
      free(serialized_data);
      if (prog) free_compiled_program(prog);
      if (src) free(src);
      return 1;
    }

    FILE *dst_exec = fopen(argv[3], "wb");
    if (!dst_exec) {
      fprintf(stderr, "Error: Failed to write output bundled executable '%s'\n",
              argv[3]);
      fclose(src_exec);
      free(serialized_data);
      if (prog) free_compiled_program(prog);
      if (src) free(src);
      return 1;
    }

    char copy_buf[4096];
    size_t bytes;
    while ((bytes = fread(copy_buf, 1, sizeof(copy_buf), src_exec)) > 0) {
      fwrite(copy_buf, 1, bytes, dst_exec);
    }
    fclose(src_exec);

    fclose(dst_exec);

    // 2. Write CURI payload to a temporary file
    FILE *tmp_cbc = fopen("main.curi", "wb");
    if (!tmp_cbc) {
      fprintf(stderr, "Error: Failed to write temporary payload file.\n");
      return 1;
    }
    fwrite(serialized_data, 1, serialized_size, tmp_cbc);
    fclose(tmp_cbc);

    // 3. Use system zip to inject payload into the APE binary
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "zip -qA %s main.curi", argv[3]);
    if (system(cmd) != 0) {
      fprintf(
          stderr,
          "Error: Failed to inject payload into binary using zip command.\n");
    }

    // 4. Handle JSON and Disks
    FILE* check_json = fopen("curica.env.json", "r");
    if (check_json) {
        fclose(check_json);
        char env_cmd[1024];
        snprintf(env_cmd, sizeof(env_cmd), "zip -qA %s curica.env.json", argv[3]);
        if (system(env_cmd) != 0) {
            fprintf(stderr, "Warning: Failed to inject curica.env.json into binary.\n");
        }
    }

    FILE *vfs_txt = fopen(".curica_vfs.txt", "w");
    bool has_disks = false;

    for (int i = 4; i < argc; i++) {
      if (strncmp(argv[i], "--attach", 8) == 0 && i + 1 < argc) {
        char *disk_arg = argv[i + 1];
        i++;
        char disk_copy[1024];
        strncpy(disk_copy, disk_arg, sizeof(disk_copy) - 1);
        disk_copy[sizeof(disk_copy) - 1] = '\0';

        char *path = disk_copy;
        char *name = strchr(path, ':');
        if (name) {
          *name = '\0';
          name++;
          char *mode = strchr(name, ':');
          if (mode) {
            *mode = '\0';
            mode++;

            fprintf(vfs_txt, "%s:%s\n", name, mode);
            has_disks = true;

            char abs_out[1024];
            realpath(argv[3], abs_out);
            char zip_cmd[2048];
            snprintf(zip_cmd, sizeof(zip_cmd),
                     "mkdir -p /tmp/curica_zip_stage_%s && cp -R %s "
                     "/tmp/curica_zip_stage_%s/%s && cd "
                     "/tmp/curica_zip_stage_%s && zip -qA -r %s %s",
                     name, path, name, name, name, abs_out, name);
            system(zip_cmd);
          }
        }
      }
    }
    fclose(vfs_txt);

    if (has_disks) {
      char cmd2[1024];
      snprintf(cmd2, sizeof(cmd2), "zip -qA %s .curica_vfs.txt", argv[3]);
      system(cmd2);
    }
    remove(".curica_vfs.txt");

    // Cleanup temporary file
    remove("main.curi");

    // 5. Make the compiled target binary executable via chmod
    chmod(argv[3], 0755);

    free(serialized_data);
    if (prog) free_compiled_program(prog);
    if (src) free(src);
    printf("Successfully bundled standalone executable to '%s'\n", argv[3]);
    return 0;
  } else if (strcmp(command, "help") == 0) {
    printf("Curica Runtime Environment (v%s)\n", CURICA_VERSION);
    printf("Usage:\n");
    printf("  curica                             - Start interactive Curica "
           "Environment\n");
    printf("  curica run [flags] <file.js>       - Run a JavaScript file\n");
    printf("  curica exec <file.curi>            - Execute a precompiled "
           "Curica Bytecode file\n");
    printf("    Flags:\n");
    printf("      --allow-net                    - Allow network access\n");
    printf("      --allow-read                   - Allow file system read "
           "access\n");
    printf("      --allow-write                  - Allow file system write "
           "access\n");
    printf("      --allow-run                    - Allow spawning child "
           "processes\n");
    printf("      --allow-ffi                    - Allow loading dynamic "
           "C-addons\n");
    printf("      --allow-all                    - Allow all permissions\n");
    printf("  curica test <directory>            - Run tests recursively in "
           "directory\n");
    printf("  curica fmt <file.js>               - Format a JavaScript file in "
           "place\n");
    printf("  curica boot [env.json]             - Boot environment from JSON "
           "config\n");
    printf("  curica compile <in.js> <out.curi>  - Compile JS source code to "
           "Curica Bytecode\n");
    printf("  curica build <in.js> <out> [args]  - Compile JS and build a "
           "self-contained cross-platform executable\n");
    printf("    Build Args:\n");
    printf("      --attach <path>:<name>:<ro|rw> - Mount a host folder as a "
           "virtual disk in the bundle\n");
    return 0;
  } else {
    fprintf(
        stderr,
        "Unknown command '%s'\nType 'curica help' for usage instructions.\n",
        command);
    return 1;
  }

  return 0;
}
