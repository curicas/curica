// Test suite for ES2025 features implemented in Curica JS Runtime

console.log("=== ES2025 Test Suite Started ===");

// 1. Set Methods
console.log("\n--- Testing Set Methods ---");
let s1 = new Set();
s1.add(1).add(2).add(3);
console.log("Set s1 size:", s1.size); // Should be 3
console.log("Set s1 has 2:", s1.has(2)); // true
console.log("Set s1 has 4:", s1.has(4)); // false

s1.delete(2);
console.log("Set s1 size after delete(2):", s1.size); // 2
console.log("Set s1 has 2 after delete:", s1.has(2)); // false

let s2 = new Set();
s2.add(3).add(4).add(5);

let intersect = s1.intersection(s2);
console.log("Intersection size (should be 1):", intersect.size);
console.log("Intersection has 3:", intersect.has(3)); // true
console.log("Intersection has 1:", intersect.has(1)); // false

let unionSet = s1.union(s2);
console.log("Union size (should be 4):", unionSet.size);
console.log("Union has 1:", unionSet.has(1)); // true
console.log("Union has 5:", unionSet.has(5)); // true

let diff = s1.difference(s2);
console.log("Difference size (should be 1):", diff.size);
console.log("Difference has 1:", diff.has(1)); // true
console.log("Difference has 3:", diff.has(3)); // false

// 2. Float16Array
console.log("\n--- Testing Float16Array ---");
let f16 = new Float16Array(4);
f16[0] = 1.5;
f16[1] = -2.75;
f16[2] = 65500; // Large number near half-precision limit
f16[3] = 0.000061; // Small subnormal number

console.log("f16 length:", f16.length); // 4
console.log("f16[0] (expected ~1.5):", f16[0]);
console.log("f16[1] (expected ~-2.75):", f16[1]);
console.log("f16[2] (expected ~65500):", f16[2]);
console.log("f16[3] (expected ~0.000061):", f16[3]);

// 3. Array Iterator Helpers (map/filter)
console.log("\n--- Testing Array Map/Filter ---");
let arr = [1, 2, 3, 4, 5];
let doubled = arr.map(function(x) { return x * 2; });
console.log("Doubled array elements:");
for (let i = 0; i < doubled.length; i = i + 1) {
    console.log("  doubled[" + i + "] =", doubled[i]);
}

let evens = arr.filter(function(x) {
    // Basic modulo simulation if % is not supported or direct check
    // Since modulo 2 is simple:
    if (x == 2) { return true; }
    if (x == 4) { return true; }
    return false;
});
console.log("Filtered evens length (expected 2):", evens.length);
console.log("Filtered evens [0] (expected 2):", evens[0]);
console.log("Filtered evens [1] (expected 4):", evens[1]);

// 4. Promise.try and .then
console.log("\n--- Testing Promise.try and .then ---");
let pSuccess = Promise.try(function() {
    return 42;
});
pSuccess.then(function(val) {
    console.log("Promise.try success resolved value (expected 42):", val);
});

let pFail = Promise.try(function() {
    // Trigger error throw inside the callback to test trapping
    throwError("Trapped custom error!");
});
pFail.then(function(val) {
    console.log("Should not be here!");
}, function(err) {
    console.log("Promise.try trapped error successfully:", err);
});

console.log("\n=== ES2025 Test Suite Completed ===");
