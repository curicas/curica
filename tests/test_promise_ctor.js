console.log("=== Testing Promise Constructor ===");

let p1 = new Promise(function(resolve, reject) {
    resolve(42);
});

p1.then(function(val) {
    console.log("p1 resolved with: " + val);
});

let p2 = new Promise(function(resolve, reject) {
    reject("Failed");
});

p2.then(null, function(err) {
    console.log("p2 rejected with: " + err);
});

// Test withResolvers
let resolvers = Promise.withResolvers();
let promise = resolvers.promise;
let resolve = resolvers.resolve;

promise.then(function(val) {
    console.log("withResolvers resolved with: " + val);
});
resolve("Hello from withResolvers");

console.log("=== End Promise Constructor Test ===");
