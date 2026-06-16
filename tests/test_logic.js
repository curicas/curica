console.log("=== Testing Logical Operators ===");

let a = true || false;
console.log("true || false: " + a);

let b = false || true;
console.log("false || true: " + b);

let c = true && false;
console.log("true && false: " + c);

let d = false && true;
console.log("false && true: " + d);

let shortCircuit = false;
let res = true || (shortCircuit = true);
console.log("shortCircuit after true ||: " + shortCircuit);

let shortCircuitAnd = true;
let res2 = false && (shortCircuitAnd = false);
console.log("shortCircuitAnd after false &&: " + shortCircuitAnd);

let complex = (false || true) && (true || false);
console.log("complex (should be true): " + complex);

let x = "a";
let test = x == "a" || x == "b" || x == "c";
console.log("chained ||: " + test);

console.log("=== End Logical Test ===");
