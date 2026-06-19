var failed = false;
function assert(cond, msg) {
    if (!cond) { console.log("FAIL: " + msg); failed = true; }
}

// Object Utilities
var o1 = { a: 1 };
var o2 = { b: 2 };
var o3 = Object.assign(o1, o2);
assert(o3.a === 1 && o3.b === 2, "Object.assign");

var entries = Object.entries(o3);
assert(entries.length === 2, "Object.entries length");

var vals = Object.values(o3);
assert(vals.length === 2 && vals[0] === 1, "Object.values");

var o4 = Object.fromEntries([["x", 10], ["y", 20]]);
assert(o4.x === 10 && o4.y === 20, "Object.fromEntries");

var o5 = Object.freeze({ z: 1 });
assert(o5.z === 1, "Object.freeze");

// String Extraction
var s = "hello world";
assert(s.charAt(1) === "e", "String.charAt");
assert(s.charCodeAt(1) === 101, "String.charCodeAt");
assert(s.at(-1) === "d", "String.at");

var s3 = "a b a b".replaceAll("a", "c");
assert(s3 === "c b c b", "String.replaceAll");

var s4 = "hello".concat(" ", "world");
assert(s4 === "hello world", "String.concat");

assert("hello world".includes("world"), "String.includes");

// Set Methods
var set1 = new Set();
set1.add(1); set1.add(2);
var set2 = new Set();
set2.add(2); set2.add(3);

var count = 0;
set1.forEach(function(v) { count += v; });
assert(count === 3, "Set.forEach");

var setVals = set1.values();
assert(setVals.length === 2, "Set.values");

assert(!set1.isDisjointFrom(set2), "Set.isDisjointFrom false");
var set3 = new Set(); set3.add(4);
assert(set1.isDisjointFrom(set3), "Set.isDisjointFrom true");

var set4 = new Set(); set4.add(1); set4.add(2); set4.add(3);
assert(set1.isSubsetOf(set4), "Set.isSubsetOf");
assert(set4.isSupersetOf(set1), "Set.isSupersetOf");

// RegExp
var re = new RegExp("b(c+)d", "i");
assert(re.test("aBCCDe"), "RegExp.test");
var match = re.exec("aBCCDe");
assert(match != null, "RegExp.exec not null");
assert(match.length >= 2, "RegExp.exec captures");

// String replace with RegExp
var s2 = "hello world".replace("world", "planet");
assert(s2 === "hello planet", "String.replace");

if (failed) {
    console.log("SOME TESTS FAILED");
} else {
    console.log("ALL STDLIB TESTS PASSED");
}
