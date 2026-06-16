console.log("=== Testing Symbol Constructor ===");

let sym1 = Symbol("foo");
let sym2 = Symbol("foo");

console.log("sym1 == sym1: " + (sym1 == sym1));
console.log("sym1 == sym2: " + (sym1 == sym2));

console.log("=== End Symbol Test ===");
