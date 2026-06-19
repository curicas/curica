function assert(cond, msg) {
    if (!cond) {
        console.log("Assertion failed: " + msg);
        throw "Assertion failed: " + msg;
    }
}

// Math
console.log("Math.abs");
assert(Math.abs(-5.5) === 5.5, "Math.abs");
console.log("Math.ceil");
assert(Math.ceil(1.2) === 2, "Math.ceil");
console.log("Math.floor");
assert(Math.floor(1.8) === 1, "Math.floor");
console.log("Math.max");
assert(Math.max(1, 5, 2) === 5, "Math.max");
console.log("Math.min");
assert(Math.min(1, 5, 2) === 1, "Math.min");
console.log("Math.pow");
assert(Math.pow(2, 3) === 8, "Math.pow");
console.log("Math.sign");
assert(Math.sign(-42) === -1, "Math.sign");
assert(Math.sign(0) === 0, "Math.sign 0");
assert(Math.sign(42) === 1, "Math.sign 1");

console.log("isNaN");
assert(isNaN(NaN) === true, "isNaN");
assert(isNaN(123) === false, "isNaN number");
assert(isFinite(123) === true, "isFinite");
assert(isFinite(Infinity) === false, "isFinite inf");

console.log("Number.isNaN");
assert(Number.isNaN(NaN) === true, "Number.isNaN");
assert(Number.isNaN("NaN") === false, "Number.isNaN string");
assert(Number.isFinite(123) === true, "Number.isFinite");
assert(Number.isFinite("123") === false, "Number.isFinite string");
assert(Number.isInteger(123) === true, "Number.isInteger");
assert(Number.isInteger(123.4) === false, "Number.isInteger float");

console.log("String search");
assert("hello world".includes("world") === true, "String.includes");
assert("hello world".includes("foo") === false, "String.includes false");
assert("hello world".startsWith("hello") === true, "String.startsWith");
assert("hello world".endsWith("world") === true, "String.endsWith");

console.log("String formatting");
assert("Hello".toLowerCase() === "hello", "String.toLowerCase");
assert("Hello".toUpperCase() === "HELLO", "String.toUpperCase");
assert("  trim me  ".trim() === "trim me", "String.trim");
assert("  trim me  ".trimStart() === "trim me  ", "String.trimStart");
assert("  trim me  ".trimEnd() === "  trim me", "String.trimEnd");

console.log("Errors");
try {
    throw new TypeError("bad type");
} catch (e) {
    assert(e.name === "TypeError", "TypeError name");
    assert(e.message === "bad type", "TypeError msg");
}

console.log("ALL TESTS PASSED");
