// Curica JS Runtime test file

// Test 1: Simple arithmetic
let a = 10;
let b = 20;
let c = a + b * 3 - 5;
console.log("Test 1 (Arithmetic): c = (10 + 20 * 3 - 5) =", c);

// Test 2: Control flow
if (c > 50) {
    console.log("Test 2 (If-Else): Success - c is greater than 50");
} else {
    console.log("Test 2 (If-Else): Failure - c is not greater than 50");
}

// Test 3: Loops
let sum = 0;
for (let i = 1; i <= 5; i = i + 1) {
    sum = sum + i;
}
console.log("Test 3 (For-Loop): sum from 1 to 5 =", sum);

// Test 4: Closures and Captured Variables
function makeCounter(start) {
    let count = start;
    return function() {
        count = count + 1;
        return count;
    };
}

let counter = makeCounter(10);
console.log("Test 4 (Closures): count 1 =", counter());
console.log("Test 4 (Closures): count 2 =", counter());

// Test 5: Objects and Arrays
let obj = {
    x: 100,
    y: "Hello World"
};
console.log("Test 5 (Objects): obj.x =", obj.x, ", obj.y =", obj.y);

let arr = [10, 20, 30];
console.log("Test 5 (Arrays): arr.length =", arr.length);
console.log("Test 5 (Arrays): arr[1] =", arr[1]);
