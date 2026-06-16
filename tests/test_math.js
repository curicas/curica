console.log("=== Testing Phase 3.1: Math & Globals ===");

// 1. Math Object Methods
console.log("Math.abs(-5.5):", Math.abs(-5.5)); // 5.5
console.log("Math.ceil(1.2):", Math.ceil(1.2)); // 2
console.log("Math.floor(1.8):", Math.floor(1.8)); // 1
console.log("Math.round(1.5):", Math.round(1.5)); // 2
console.log("Math.trunc(1.9):", Math.trunc(1.9)); // 1
console.log("Math.sign(-42):", Math.sign(-42)); // -1
console.log("Math.max(1, 5, 2):", Math.max(1, 5, 2)); // 5
console.log("Math.min(1, 5, 2):", Math.min(1, 5, 2)); // 1
console.log("Math.pow(2, 3):", Math.pow(2, 3)); // 8
console.log("Math.sqrt(16):", Math.sqrt(16)); // 4
let r = Math.random();
let r_valid = false;
if (r >= 0) {
    if (r < 1) {
        r_valid = true;
    }
}
console.log("Math.random():", r_valid); // true

// 2. Global Parsers & Checks
console.log("parseInt('42', 10):", parseInt("42", 10)); // 42
console.log("parseFloat('3.1415'):", parseFloat("3.1415")); // 3.1415
console.log("isNaN(NaN):", isNaN(NaN)); // true
console.log("isNaN('foo'):", isNaN("foo")); // true
console.log("isFinite(Infinity):", isFinite(Infinity)); // false
console.log("isFinite(42):", isFinite(42)); // true

// 3. Number Object Helpers
console.log("Number.isNaN('foo'):", Number.isNaN("foo")); // false (no coercion)
console.log("Number.isNaN(NaN):", Number.isNaN(NaN)); // true
console.log("Number.isFinite('42'):", Number.isFinite("42")); // false (no coercion)
console.log("Number.isFinite(42):", Number.isFinite(42)); // true
console.log("Number.isInteger(42.5):", Number.isInteger(42.5)); // false
console.log("Number.isInteger(42.0):", Number.isInteger(42.0)); // true

console.log("=== End Phase 3.1 Test ===");
