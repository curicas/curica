const wasi = new WASI({});
console.log("wasi.start typeof:", typeof wasi.start);
if (typeof wasi.start === 'undefined') {
    console.log("wasi.start is undefined!");
} else {
    console.log("wasi.start is defined!");
}
