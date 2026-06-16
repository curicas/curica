#!/bin/bash
set -e

WAMR_DIR="$(pwd)/third_party/wamr"
BUILD_DIR="${WAMR_DIR}/product-mini/platforms/linux/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Using cosmocc as the compiler
CC="$(pwd)/../../../../../../cosmocc/bin/cosmocc"
CXX="$(pwd)/../../../../../../cosmocc/bin/cosmoc++"

cmake .. \
    -DCMAKE_C_COMPILER="$CC" \
    -DWAMR_BUILD_FAST_JIT=0 \
    -DWAMR_BUILD_FAST_INTERP=1 \
    -DWAMR_BUILD_INTERP=1 \
    -DWAMR_BUILD_LIBC_WASI=1 \
    -DWAMR_BUILD_LIBC_BUILTIN=1 \
    -DWAMR_BUILD_SIMD=0

make -j8
