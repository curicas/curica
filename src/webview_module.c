/**
 * @file webview_module.c
 * @brief Native GUI/Windowing Subsystem
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

#include "vm.h"
#include "alloc.h"

#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>

// Typedefs for GTK/WebKit functions
typedef void (*gtk_init_t)(int *argc, char ***argv);
typedef void* (*gtk_window_new_t)(int type);
typedef void (*gtk_window_set_title_t)(void *window, const char *title);
typedef void (*gtk_window_set_default_size_t)(void *window, int width, int height);
typedef void* (*webkit_web_view_new_t)(void);
typedef void (*gtk_container_add_t)(void *container, void *widget);
typedef void (*webkit_web_view_load_uri_t)(void *web_view, const char *uri);
typedef void (*gtk_widget_show_all_t)(void *widget);
typedef void (*gtk_main_t)(void);

// Curica.WebView.__webview_spawn(url, width, height, title)
/**
 * @brief Spawns a Native GTK/WebKit GUI Window natively using Cosmopolitan libc.
 * 
 * This function bypasses the need to statically link massive C++ frameworks 
 * (like Electron or Qt) directly into the Curica `ape` executable. Instead, 
 * it utilizes `cosmo_dlopen` to dynamically resolve and load the host machine's 
 * native `libwebkit2gtk-4.0.so.37` and `libgtk-3.so.0` libraries at runtime.
 * 
 * @note To prevent the blocking `gtk_main` loop from hanging the JavaScript 
 * VM's asynchronous event loop, the function utilizes `fork()`. The parent 
 * process returns immediately, while the child process initializes GTK and 
 * handles the WebKit rendering context.
 * 
 * @param vm The VM instance.
 * @param this_val The JS `this` context.
 * @param arg_count The number of arguments passed from JS.
 * @param args Array of JS arguments.
 * @return JS Value indicating success/failure.
 */
static Value webview_spawn(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 4 || !IS_POINTER(args[0]) || !IS_POINTER(args[3])) {
        return VAL_FALSE;
    }
    
    JSString* url_str = (JSString*)get_pointer(args[0]);
    int width = IS_INTEGER(args[1]) ? get_integer(args[1]) : (IS_DOUBLE(args[1]) ? (int)get_double(args[1]) : 800);
    int height = IS_INTEGER(args[2]) ? get_integer(args[2]) : (IS_DOUBLE(args[2]) ? (int)get_double(args[2]) : 600);
    JSString* title_str = (JSString*)get_pointer(args[3]);
    
    char url[1024];
    strncpy(url, url_str->data, sizeof(url) - 1);
    char title[256];
    strncpy(title, title_str->data, sizeof(title) - 1);
    
    pid_t pid = fork();
    if (pid < 0) {
        return VAL_FALSE; // Fork failed
    } else if (pid > 0) {
        // Parent process: return immediately to continue the VM event loop
        return make_integer(pid);
    }
    
    // ---------------------------------------------------------
    // Child Process: Native GUI Rendering Loop
    // ---------------------------------------------------------
    
    // Check if we are in Termux (Android)
    const char* prefix = getenv("PREFIX");
    if (prefix && strstr(prefix, "com.termux")) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "termux-open-url \"%s\"", url);
        system(cmd);
        exit(0);
    }
    
    // Dynamically load GTK3 and WebKit2GTK on Linux using cosmo_dlopen
    void* gtk = cosmo_dlopen("libgtk-3.so.0", RTLD_LAZY);
    void* webkit = cosmo_dlopen("libwebkit2gtk-4.0.so.37", RTLD_LAZY);
    if (!webkit) webkit = cosmo_dlopen("libwebkit2gtk-4.1.so.0", RTLD_LAZY);
    
    if (!gtk || !webkit) {
        fprintf(stderr, "[Curica WebView] Failed to load native GTK/WebKit libraries: %s\n", cosmo_dlerror());
        exit(1);
    }
    
    gtk_init_t gtk_init = (gtk_init_t)cosmo_dlsym(gtk, "gtk_init");
    if (!gtk_init) fprintf(stderr, "Missing gtk_init\n");
    gtk_window_new_t gtk_window_new = (gtk_window_new_t)cosmo_dlsym(gtk, "gtk_window_new");
    if (!gtk_window_new) fprintf(stderr, "Missing gtk_window_new\n");
    gtk_window_set_title_t gtk_window_set_title = (gtk_window_set_title_t)cosmo_dlsym(gtk, "gtk_window_set_title");
    gtk_window_set_default_size_t gtk_window_set_default_size = (gtk_window_set_default_size_t)cosmo_dlsym(gtk, "gtk_window_set_default_size");
    gtk_container_add_t gtk_container_add = (gtk_container_add_t)cosmo_dlsym(gtk, "gtk_container_add");
    gtk_widget_show_all_t gtk_widget_show_all = (gtk_widget_show_all_t)cosmo_dlsym(gtk, "gtk_widget_show_all");
    gtk_main_t gtk_main = (gtk_main_t)cosmo_dlsym(gtk, "gtk_main");
    if (!gtk_main) fprintf(stderr, "Missing gtk_main\n");
    
    webkit_web_view_new_t webkit_web_view_new = (webkit_web_view_new_t)cosmo_dlsym(webkit, "webkit_web_view_new");
    if (!webkit_web_view_new) fprintf(stderr, "Missing webkit_web_view_new\n");
    webkit_web_view_load_uri_t webkit_web_view_load_uri = (webkit_web_view_load_uri_t)cosmo_dlsym(webkit, "webkit_web_view_load_uri");
    if (!webkit_web_view_load_uri) fprintf(stderr, "Missing webkit_web_view_load_uri\n");
    
    if (!gtk_init || !gtk_window_new || !webkit_web_view_new || !gtk_main) {
        fprintf(stderr, "[Curica WebView] Failed to resolve WebKitGTK symbols.\n");
        exit(1);
    }
    
    int c_argc = 0;
    gtk_init(&c_argc, NULL);
    void* window = gtk_window_new(0); // GTK_WINDOW_TOPLEVEL
    gtk_window_set_title(window, title);
    gtk_window_set_default_size(window, width, height);
    
    void* webview = webkit_web_view_new();
    gtk_container_add(window, webview);
    webkit_web_view_load_uri(webview, url);
    
    gtk_widget_show_all(window);
    
    // Block indefinitely on the UI loop. When the window is closed, gtk_main doesn't automatically exit
    // unless we connect the "destroy" signal. For this basic implementation, we just run gtk_main.
    // To make it exit cleanly, we really should connect the destroy signal to gtk_main_quit.
    // Instead, since it's a child process, when the parent dies or window closes, we can handle it.
    // Actually, GTK requires "destroy" -> gtk_main_quit to exit gtk_main().
    // We will dynamically load g_signal_connect_data to do it properly!
    
    typedef void (*gtk_main_quit_t)(void);
    gtk_main_quit_t gtk_main_quit = (gtk_main_quit_t)cosmo_dlsym(gtk, "gtk_main_quit");
    
    typedef unsigned long (*g_signal_connect_data_t)(void*, const char*, void*, void*, void*, int);
    void* gobject = cosmo_dlopen("libgobject-2.0.so.0", RTLD_LAZY);
    if (gobject && gtk_main_quit) {
        g_signal_connect_data_t g_signal_connect_data = (g_signal_connect_data_t)cosmo_dlsym(gobject, "g_signal_connect_data");
        if (g_signal_connect_data) {
            g_signal_connect_data(window, "destroy", (void*)gtk_main_quit, NULL, NULL, 0);
        }
    }
    
    gtk_main();
    exit(0);
    return VAL_UNDEFINED;
}

void vm_register_webview(VM* vm) {
    Value curica_obj = object_get(vm->global_obj, create_string("Curica", 6));
    if (!IS_POINTER(curica_obj)) {
        curica_obj = create_object();
        object_set(vm->global_obj, create_string("Curica", 6), curica_obj);
    }
    
    Value webview_obj = create_object();
    object_set(webview_obj, create_string("__webview_spawn", 15), create_native_function((void*)webview_spawn, create_string("__webview_spawn", 15)));
    
    object_set(curica_obj, create_string("WebView", 7), webview_obj);
}
