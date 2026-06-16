console.log("Reading fetch globally:");
var f = fetch;
console.log("f is:", f);

console.log("Calling fetch globally...");
var p = fetch("http://httpbin.org/get");
console.log("fetch returned:", p);
