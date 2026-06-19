const assert = (cond, msg) => {
    if (!cond) throw new Error("Assertion failed: " + msg);
};

// Test Array.of
let arr1 = Array.of(1, 2, 3);
assert(arr1.length === 3 && arr1[0] === 1 && arr1[2] === 3, "Array.of");

// Test Array() constructor
let arr2 = Array(5);
assert(arr2.length === 5 && arr2[0] === undefined, "Array constructor len");
let arr3 = new Array(1, 2);
assert(arr3.length === 2 && arr3[1] === 2, "Array constructor args");

// Test Array.from
let arr4 = Array.from([10, 20, 30], x => x * 2);
assert(arr4.length === 3 && arr4[0] === 20 && arr4[2] === 60, "Array.from array");

// Test flatMap
let arr5 = [1, 2].flatMap(x => [x, x * 2]);
assert(arr5.length === 4 && arr5[0] === 1 && arr5[1] === 2 && arr5[2] === 2 && arr5[3] === 4, "flatMap");

// Test sort (default)
let arr6 = [10, 2, 30].sort();
assert(arr6[0] === 10 && arr6[1] === 2 && arr6[2] === 30, "sort default");

// Test sort (custom)
let arr7 = [10, 2, 30].sort((a, b) => a - b);
assert(arr7[0] === 2 && arr7[1] === 10 && arr7[2] === 30, "sort custom asc");

let arr8 = [10, 2, 30].sort((a, b) => b - a);
assert(arr8[0] === 30 && arr8[1] === 10 && arr8[2] === 2, "sort custom desc");

// Test splice
let arr9 = [1, 2, 3, 4];
let del = arr9.splice(1, 2, 'a', 'b');
assert(arr9.length === 4 && arr9[0] === 1 && arr9[1] === 'a' && arr9[2] === 'b' && arr9[3] === 4, "splice modify");
assert(del.length === 2 && del[0] === 2 && del[1] === 3, "splice return");

console.log("All tests passed!");
