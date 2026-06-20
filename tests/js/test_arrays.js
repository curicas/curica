var a = [1, 2, 3];
var mapped = a.map(function(x) { return x * 2; });
if (mapped[0] !== 2 || mapped[2] !== 6) throw new Error("map failed");

var filtered = a.filter(function(x) { return x > 1; });
if (filtered.length !== 2 || filtered[0] !== 2) throw new Error("filter failed");

var sum = 0;
a.forEach(function(x) { sum += x; });
if (sum !== 6) throw new Error("forEach failed");

var hasTwo = a.includes(2);
var noFour = a.includes(4);
if (!hasTwo || noFour) throw new Error("includes failed");

var idx = a.indexOf(3);
if (idx !== 2) throw new Error("indexOf failed");

var lastIdx = [1, 2, 1].lastIndexOf(1);
if (lastIdx !== 2) throw new Error("lastIndexOf failed");

var reduced = a.reduce(function(acc, val) { return acc + val; }, 10);
if (reduced !== 16) throw new Error("reduce failed");

var isAllPositive = a.every(function(x) { return x > 0; });
if (!isAllPositive) throw new Error("every failed");

var hasThree = a.some(function(x) { return x === 3; });
if (!hasThree) throw new Error("some failed");

var found = a.find(function(x) { return x === 2; });
if (found !== 2) throw new Error("find failed");

var foundIdx = a.findIndex(function(x) { return x === 2; });
if (foundIdx !== 1) throw new Error("findIndex failed");

var popped = a.pop();
if (popped !== 3 || a.length !== 2) throw new Error("pop failed");

var shifted = a.shift();
if (shifted !== 1 || a.length !== 1) throw new Error("shift failed");

var unshifted = a.unshift(5, 6);
if (unshifted !== 3 || a[0] !== 5 || a[1] !== 6 || a[2] !== 2) throw new Error("unshift failed: " + a.join(','));

var rev = [1, 2, 3].reverse();
if (rev[0] !== 3 || rev[2] !== 1) throw new Error("reverse failed");

var sliced = [1, 2, 3, 4].slice(1, 3);
if (sliced.length !== 2 || sliced[0] !== 2 || sliced[1] !== 3) throw new Error("slice failed");

var concat = [1, 2].concat([3, 4], 5);
if (concat.length !== 5 || concat[4] !== 5) throw new Error("concat failed");

var flat = [1, [2, 3], 4].flat();
if (flat.length !== 4 || flat[1] !== 2 || flat[3] !== 4) throw new Error("flat failed");

console.log("ARRAY TESTS PASSED");
