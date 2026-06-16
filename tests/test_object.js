console.log("=== Testing Object Statics ===");

let obj = { a: 1, b: 2 };
let obj2 = { c: 3 };

console.log("1");
let keys = Object.keys(obj);
console.log("keys: " + keys.join(", "));

console.log("2");
let values = Object.values(obj);
console.log("values: " + values.join(", "));

console.log("3");
let entries = Object.entries(obj);
console.log("entries length: " + entries.length);
if (entries.length > 0) {
    console.log("entries[0]: " + entries[0][0] + "=" + entries[0][1]);
}

console.log("4");
let target = { x: 10 };
Object.assign(target, obj, obj2);
console.log("assign target: x=" + target.x + " a=" + target.a + " c=" + target.c);

console.log("5");
let fromEnt = Object.fromEntries([["hello", "world"], ["foo", "bar"]]);
console.log("fromEntries hello: " + fromEnt.hello);

console.log("6");
let empty = Object.create();
Object.assign(empty, {y: 100});
console.log("create empty y: " + empty.y);

console.log("=== End Object Test ===");
