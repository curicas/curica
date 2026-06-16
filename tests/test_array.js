console.log("=== Testing Array Methods ===");

let arr = [1, 2, 3, 4, 5];

console.log("at(2): " + arr.at(2));
console.log("at(-1): " + arr.at(-1));

console.log("isArray([1,2]): " + Array.isArray([1, 2]));
console.log("isArray('hello'): " + Array.isArray("hello"));

console.log("from('hello'): " + Array["from"]("hello").join("-"));
console.log("of(1, 2, 3): " + Array["of"](1, 2, 3).join(", "));

arr.push(6, 7);
console.log("push(6, 7): " + arr.join(", "));
console.log("pop(): " + arr.pop());
console.log("after pop: " + arr.join(", "));

let strArr = ["apple", "banana", "cherry"];
strArr.reverse();
console.log("reverse: " + strArr.join(", "));

strArr.sort();
console.log("sort: " + strArr.join(", "));

let spliced = arr.splice(2, 2, "a", "b");
console.log("splice(2, 2, 'a', 'b'): " + arr.join(", "));
console.log("spliced elements: " + spliced.join(", "));

let sliceArr = arr.slice(1, 4);
console.log("slice(1, 4): " + sliceArr.join(", "));

console.log("includes('a'): " + arr.includes("a"));
console.log("indexOf('a'): " + arr.indexOf("a"));

let concatArr = arr.concat(["x", "y"], "z");
console.log("concat: " + concatArr.join(", "));

let fillArr = [1, 1, 1, 1, 1];
fillArr.fill(0, 1, 4);
console.log("fill(0, 1, 4): " + fillArr.join(", "));

let isPositive = function(x) { 
    if (x > 0) return true;
    if (x == "a") return true;
    if (x == "b") return true;
    return false;
};
console.log("every(x > 0): " + arr.every(isPositive));

let isB = function(x) { return x == "b"; };
console.log("some(x == 'b'): " + arr.some(isB));

let found = arr.find(isB);
let foundIdx = arr.findIndex(isB);
console.log("find 'b': " + found + " at index " + foundIdx);

let sumFn = function(acc, curr) { return acc + curr; };
let sum = [1, 2, 3].reduce(sumFn, 0);
console.log("reduce sum: " + sum);

let flatArr = [1, [2, [3, 4]], 5];
console.log("flat(1): " + flatArr.flat(1).join(", "));
console.log("flat(2): " + flatArr.flat(2).join(", "));

console.log("=== End Array Test ===");
