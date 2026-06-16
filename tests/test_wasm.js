const fs = require('fs');

console.log("Testing WebAssembly...");

// A simple WebAssembly module that exports an `add(a, b)` function.
// (module
//   (func $add (param $a i32) (param $b i32) (result i32)
//     local.get $a
//     local.get $b
//     i32.add)
//   (export "add" (func $add))
// )
const wasmBuffer = Buffer.from([
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // Magic & Version
  0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, // Type section
  0x03, 0x02, 0x01, 0x00, // Function section
  0x07, 0x07, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00, // Export section
  0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b // Code section
]);

try {
    var result = WebAssembly.instantiate(wasmBuffer);
    if (result && result.instance) {
        console.log("result.instance:", result.instance);
        var sum = result.instance.getFunction("add", 5, 7);
        if (sum == 12) {
            console.log("Test Passed!");
        } else {
            console.log("Failed: expected 12, got", sum);
        }
    } else {
        console.log("Failed to instantiate WebAssembly module.");
    }
} catch (e) {
    console.error("Exception:", e);
}
