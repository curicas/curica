#!/bin/bash
set -e

WAMR_DIR="third_party/wamr/core"
BUILD_DIR="build/wamr"
mkdir -p "$BUILD_DIR"

CC="./cosmocc/bin/cosmocc"
AS="./cosmocc/bin/x86_64-linux-cosmo-as"
AR="./cosmocc/bin/cosmoar"

# Collect all WAMR C source files needed for Fast Interpreter + WASI + libc builtin
C_FILES=(
    ${WAMR_DIR}/iwasm/interpreter/wasm_interp_fast.c
    ${WAMR_DIR}/iwasm/interpreter/wasm_loader.c
    ${WAMR_DIR}/iwasm/interpreter/wasm_mini_loader.c
    ${WAMR_DIR}/iwasm/interpreter/wasm_runtime.c
    ${WAMR_DIR}/iwasm/common/wasm_application.c
    ${WAMR_DIR}/iwasm/common/wasm_c_api.c
    ${WAMR_DIR}/iwasm/common/wasm_exec_env.c
    ${WAMR_DIR}/iwasm/common/wasm_memory.c
    ${WAMR_DIR}/iwasm/common/wasm_native.c
    ${WAMR_DIR}/iwasm/common/wasm_runtime_common.c
    ${WAMR_DIR}/iwasm/common/wasm_shared_memory.c
    ${WAMR_DIR}/iwasm/common/wasm_blocking_op.c
    ${WAMR_DIR}/shared/mem-alloc/mem_alloc.c
    ${WAMR_DIR}/shared/mem-alloc/ems/ems_alloc.c
    ${WAMR_DIR}/shared/mem-alloc/ems/ems_hmu.c
    ${WAMR_DIR}/shared/mem-alloc/ems/ems_kfc.c
    ${WAMR_DIR}/shared/utils/bh_assert.c
    ${WAMR_DIR}/shared/utils/bh_bitmap.c
    ${WAMR_DIR}/shared/utils/bh_common.c
    ${WAMR_DIR}/shared/utils/bh_hashmap.c
    ${WAMR_DIR}/shared/utils/bh_list.c
    ${WAMR_DIR}/shared/utils/bh_log.c
    ${WAMR_DIR}/shared/utils/bh_queue.c
    ${WAMR_DIR}/shared/utils/bh_vector.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_blocking_op.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_clock.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_file.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_malloc.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_memmap.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_sleep.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_socket.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_thread.c
    ${WAMR_DIR}/shared/platform/common/posix/posix_time.c
    ${WAMR_DIR}/shared/platform/linux/platform_init.c
    ${WAMR_DIR}/iwasm/libraries/libc-builtin/libc_builtin_wrapper.c
    ${WAMR_DIR}/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/posix.c
    ${WAMR_DIR}/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/random.c
    ${WAMR_DIR}/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/str.c
    ${WAMR_DIR}/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/blocking_op.c
    ${WAMR_DIR}/iwasm/libraries/libc-wasi/libc_wasi_wrapper.c
    ${WAMR_DIR}/shared/utils/runtime_timer.c
    ${WAMR_DIR}/shared/platform/common/libc-util/libc_errno.c
    ${WAMR_DIR}/iwasm/common/arch/invokeNative_general.c
)

INCLUDES="-Isrc/compat -I${WAMR_DIR}/iwasm/include -I${WAMR_DIR}/iwasm/common -I${WAMR_DIR}/iwasm/interpreter -I${WAMR_DIR}/shared/platform/include -I${WAMR_DIR}/shared/platform/linux -I${WAMR_DIR}/shared/platform/common/libc-util -I${WAMR_DIR}/shared/utils -I${WAMR_DIR}/shared/mem-alloc -I${WAMR_DIR}/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include -I${WAMR_DIR}/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src"

CFLAGS="-g -O3 ${WARNING_FLAGS} -std=gnu99 -DWASM_ENABLE_FAST_INTERP=1 -DWASM_ENABLE_INTERP=1 -DWASM_ENABLE_LIBC_WASI=1 -DWASM_ENABLE_LIBC_BUILTIN=1 -DWASMTIME_SSP_STATIC_WATERMARKS=1 -DWASM_ENABLE_AOT=0 -DWASM_ENABLE_FAST_JIT=0 -DWASM_ENABLE_JIT=0 -DBH_MALLOC=wasm_runtime_malloc -DBH_FREE=wasm_runtime_free -DBH_PLATFORM_LINUX -DWASM_ENABLE_MODULE_INST_CONTEXT=1 -DWASM_ENABLE_SIMD=1"

OBJ_FILES=()

# Compile C files
for cfile in "${C_FILES[@]}"; do
    filename=$(basename "$cfile")
    objfile="$BUILD_DIR/${filename}.o"
    echo "Compiling $cfile"
    $CC $CFLAGS $INCLUDES -c "$cfile" -o "$objfile"
    OBJ_FILES+=("$objfile")
done

# Archive into static library
rm -f build/libwamr.a
$AR rcs build/libwamr.a "${OBJ_FILES[@]}"
echo "Successfully built build/libwamr.a"
