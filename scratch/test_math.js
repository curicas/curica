var failed = false;
function assert(cond, msg) {
    if (!cond) { console.log("FAIL: " + msg); failed = true; }
}

assert(Math.abs(-5) === 5, "Math.abs");
assert(Math.ceil(1.2) === 2, "Math.ceil");
assert(Math.floor(1.8) === 1, "Math.floor");
assert(Math.round(1.5) === 2, "Math.round");
assert(Math.max(1, 5, 2) === 5, "Math.max");
assert(Math.min(1, 5, 2) === 1, "Math.min");
assert(Math.pow(2, 3) === 8, "Math.pow");
assert(Math.sqrt(9) === 3, "Math.sqrt");
assert(Math.cbrt(27) === 3, "Math.cbrt");
assert(Math.sign(-42) === -1, "Math.sign");
assert(Math.trunc(1.99) === 1, "Math.trunc");
assert(Math.sin(0) === 0, "Math.sin");
assert(Math.cos(0) === 1, "Math.cos");

if (failed) {
    console.log("SOME MATH TESTS FAILED");
} else {
    console.log("ALL MATH TESTS PASSED");
}
