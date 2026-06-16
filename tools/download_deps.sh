#!/bin/bash
set -e


# Setup script to download and unpack the cosmocc compiler toolchain.

COSMOCC_DIR="cosmocc"

if [ -d "$COSMOCC_DIR/bin/cosmocc" ]; then
    echo "cosmocc compiler already exists. Skipping download."
else
    echo "Creating cosmocc directory..."
mkdir -p "$COSMOCC_DIR"
cd "$COSMOCC_DIR"

echo "Downloading cosmocc zip archive..."
# Fallback chain for reliability
if curl -f -L -o cosmocc.zip "https://github.com/jart/cosmopolitan/releases/download/3.3.3/cosmocc-3.3.3.zip"; then
    echo "Download finished."
else
    echo "Failed to download cosmocc."
    exit 1
fi

echo "Unzipping cosmocc..."
unzip -q -o cosmocc.zip
rm cosmocc.zip

cd ..
echo "cosmocc toolchain successfully set up in ./cosmocc"
fi

mkdir -p third_party/sqlite third_party/wasm3
cd third_party

echo "Downloading SQLite amalgamation..."
curl -sL https://sqlite.org/2024/sqlite-amalgamation-3450200.zip -o sqlite.zip
unzip -q sqlite.zip
cp sqlite-amalgamation-3450200/sqlite3.c sqlite/
cp sqlite-amalgamation-3450200/sqlite3.h sqlite/
cp sqlite-amalgamation-3450200/sqlite3ext.h sqlite/
rm -rf sqlite.zip sqlite-amalgamation-3450200

echo "Downloading Wasm3..."
curl -sL https://github.com/wasm3/wasm3/archive/refs/tags/v0.5.0.zip -o wasm3.zip
unzip -q wasm3.zip
cp -r wasm3-0.5.0/source/* wasm3/
rm -rf wasm3.zip wasm3-0.5.0

echo "Downloading mbedTLS..."
curl -sL https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v3.5.0.zip -o mbedtls.zip
unzip -q mbedtls.zip
mv mbedtls-3.5.0 mbedtls
rm -rf mbedtls.zip

echo "Downloading WAMR..."
curl -sL https://github.com/bytecodealliance/wasm-micro-runtime/archive/refs/tags/WAMR-1.3.0.zip -o wamr.zip
unzip -q wamr.zip
mv wasm-micro-runtime-WAMR-1.3.0 wamr
rm -rf wamr.zip

echo "Downloading llama.cpp..."
curl -sL https://github.com/ggml-org/llama.cpp/archive/refs/heads/master.zip -o llama.zip
unzip -q llama.zip
mv llama.cpp-master llama.cpp
rm -rf llama.zip

echo "Dependencies downloaded successfully."
