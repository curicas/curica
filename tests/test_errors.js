console.log("=== Testing Error Constructors ===");

let err = new Error("Something went wrong");
console.log("Error name: " + err.name);
console.log("Error message: " + err.message);

let typeErr = new TypeError("Invalid type");
console.log("TypeError name: " + typeErr.name);
console.log("TypeError message: " + typeErr.message);

let rangeErr = new RangeError("Out of bounds");
console.log("RangeError name: " + rangeErr.name);

let suppErr = new SuppressedError(new Error("original"), new Error("suppressed"), "Suppression failed");
console.log("SuppressedError name: " + suppErr.name);
console.log("SuppressedError message: " + suppErr.message);

console.log("=== End Error Test ===");
