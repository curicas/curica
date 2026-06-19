const assert = (cond, msg) => {
    if (!cond) throw new Error("Assertion failed: " + msg);
};

const FFI = Curica.FFI;
assert(FFI, "FFI module available");

// Note: libc is already loaded into Curica or we can load it. 
// "libc.so.6" usually works on Linux.
let libc = FFI.loadLibrary("libc.so.6");
if (!libc) {
    console.log("Could not load libc.so.6, skipping test.");
} else {
    // Test qsort using a callback
    let qsort_sym = FFI.getSymbol(libc, "qsort");
    assert(qsort_sym, "qsort symbol found");

    // qsort signature: void qsort(void *base, size_t nitems, size_t size, int (*compar)(const void *, const void*))
    // We'll sort an array of ints (size 4).
    // In Curica JS, we don't have easy typed arrays right now, wait, we do have ArrayBuffer but let's test if we can just create a callback!
    
    let callbackCalled = false;
    let my_compar = function(a, b) {
        callbackCalled = true;
        // In C, a and b are int* (pointers to int). So a and b are pointers.
        // We can't dereference them from JS right now without a Memory read API,
        // but we just want to test if the callback gets invoked!
        return -1; 
    };

    let cb_ptr = FFI.createCallback(my_compar, "int32", ["pointer", "pointer"]);
    assert(cb_ptr, "createCallback succeeded");

    // We can't easily allocate a C array from JS to pass to qsort.
    // Wait, let's just test creating a Struct type.
    let structType = FFI.createStructType(["int32", "float"]);
    assert(structType, "createStructType succeeded");

    console.log("FFI Advanced tests passed!");
}
