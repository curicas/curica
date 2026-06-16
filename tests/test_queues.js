console.log("1. Sync");

setTimeout(function() {
    console.log("4. setTimeout");
}, 0);

setImmediate(function() {
    console.log("5. setImmediate");
});

queueMicrotask(function() {
    console.log("3. queueMicrotask");
});

process.nextTick(function() {
    console.log("2. process.nextTick");
});
