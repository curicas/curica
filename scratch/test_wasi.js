const fs = require('fs');
console.log("1. Got FS");
try { fs.mkdirSync("./scratch/wasi_sandbox"); } catch(e) {}
console.log("2. Mkdir done");
fs.writeFileSync("./scratch/wasi_sandbox/secret.txt", "This is securely isolated.");
console.log("3. Wrote secret");

const pyScript = `import os\nprint('Hello')`;
fs.writeFileSync("./scratch/wasi_sandbox/test_python.py", pyScript);
console.log("4. Wrote py script");

let python_wasm = fs.readFileSync("./vfs/bin/main.wasm");
console.log("5. Read python_wasm, type:", typeof python_wasm, "length:", python_wasm ? python_wasm.length : "null");

const wasi = new WASI({
  args: ['python', '/sandbox/test_python.py'],
  env: { PWD: '/sandbox', WASI_SDK_PATH: '/sandbox/vfs' },
  preopens: {
    '/sandbox': './scratch/wasi_sandbox',
    '/lib': './vfs/lib'
  }
});
console.log("6. Created WASI instance");

const importObject = { wasi_snapshot_preview1: wasi.wasiImport };
console.log("7. Calling instantiate...");
let result = WebAssembly.instantiate(python_wasm, importObject);
console.log("8. Instantiate done, result:", typeof result);

if (result) {
    console.log("9. Calling wasi.start...");
    wasi.start(result.instance);
    console.log("10. wasi.start done");
}
