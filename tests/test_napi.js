var module = { exports: {} };
console.log("Before dlopen");
process.dlopen(module, "./test_addon.so");
console.log("Addon loaded. hello() returns:", module.exports.hello());
