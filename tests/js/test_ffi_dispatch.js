const FFI = Curica.FFI;

let libm = FFI.loadLibrary("libm.so.6");
if (!libm) libm = FFI.loadLibrary("libm.so");

if (libm) {
    let pow_sym = FFI.getSymbol(libm, "pow");
    if (pow_sym) {
        let result = FFI.callSymbol(pow_sym, "double", ["double", "double"], [2.0, 10.0]);
        console.log("pow(2, 10) =", result);
    } else {
        console.log("pow symbol not found in libm");
    }
} else {
    console.log("Failed to load libm");
}

let libc = FFI.loadLibrary("libc.so.6");
if (libc) {
    let puts_sym = FFI.getSymbol(libc, "puts");
    if (puts_sym) {
        // We will pass the JSString object to C, which is dangerous since C expects a raw char* pointer.
        // Let's pass a pointer to something else if we can. Or just test pow for now.
    }
}
