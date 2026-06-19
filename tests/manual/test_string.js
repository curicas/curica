const assert = (cond, msg) => {
    if (!cond) throw new Error("Assertion failed: " + msg);
};

// Test String() constructor
let s1 = String(123);
assert(s1 === "123", "String constructor");

// Test startsWith
assert("abc".startsWith("a"), "startsWith 1");
assert("abc".startsWith("b", 1), "startsWith 2");
assert(!"abc".startsWith("c", 1), "startsWith 3");

// Test endsWith
assert("abc".endsWith("c"), "endsWith 1");
assert("abc".endsWith("b", 2), "endsWith 2");
assert(!"abc".endsWith("a", 2), "endsWith 3");

// Test lastIndexOf
assert("abcabc".lastIndexOf("abc") === 3, "lastIndexOf 1");
assert("abcabc".lastIndexOf("abc", 2) === 0, "lastIndexOf 2");
assert("abcabc".lastIndexOf("z") === -1, "lastIndexOf 3");

// Test padStart
assert("a".padStart(3, "x") === "xxa", "padStart 1");
assert("a".padStart(1, "x") === "a", "padStart 2");
assert("a".padStart(4) === "   a", "padStart 3");

// Test padEnd
assert("a".padEnd(3, "x") === "axx", "padEnd 1");
assert("a".padEnd(4) === "a   ", "padEnd 2");

// Test toLowerCase
assert("AbC".toLowerCase() === "abc", "toLowerCase");

// Test toUpperCase
assert("AbC".toUpperCase() === "ABC", "toUpperCase");

// Test trim
assert("  abc  ".trim() === "abc", "trim 1");
assert("abc".trim() === "abc", "trim 2");

// Test trimStart
assert("  abc  ".trimStart() === "abc  ", "trimStart");

// Test trimEnd
assert("  abc  ".trimEnd() === "  abc", "trimEnd");

console.log("All String tests passed!");
