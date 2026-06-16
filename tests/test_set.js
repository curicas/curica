console.log("=== Testing Set Methods ===");

let s1 = new Set();
s1.add(1);
s1.add(2);
s1.add(3);

let s2 = new Set();
s2.add(2);
s2.add(3);
s2.add(4);

console.log("s1 values: " + s1.values().join(", "));
console.log("s2 values: " + s2.values().join(", "));

s1.forEach(function(val) { console.log("forEach val: " + val); });

console.log("s1.isDisjointFrom(s2): " + s1.isDisjointFrom(s2)); // false
let s3 = new Set(); s3.add(10);
console.log("s1.isDisjointFrom(s3): " + s1.isDisjointFrom(s3)); // true

let subSet = new Set(); subSet.add(1); subSet.add(2);
console.log("subSet.isSubsetOf(s1): " + subSet.isSubsetOf(s1)); // true
console.log("s1.isSubsetOf(subSet): " + s1.isSubsetOf(subSet)); // false

console.log("s1.isSupersetOf(subSet): " + s1.isSupersetOf(subSet)); // true
console.log("subSet.isSupersetOf(s1): " + subSet.isSupersetOf(s1)); // false

let symDiff = s1.symmetricDifference(s2);
console.log("symmetricDifference(s1, s2): " + symDiff.values().join(", ")); // 1, 4

let inter = s1.intersection(s2);
console.log("intersection(s1, s2): " + inter.values().join(", ")); // 2, 3

let un = s1.union(s2);
console.log("union(s1, s2): " + un.values().join(", ")); // 1, 2, 3, 4

let diff = s1.difference(s2);
console.log("difference(s1, s2): " + diff.values().join(", ")); // 1

console.log("=== End Set Test ===");
