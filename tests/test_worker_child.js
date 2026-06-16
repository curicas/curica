// test_worker_child.js

onmessage = function(msg) {
    console.log("Worker: Received message:", msg);
    
    // Simulate some work
    console.log("Worker: Computing heavy task...");
    var sum = 0;
    for (var i = 0; i < 100000; i = i + 1) {
        sum = sum + i;
    }
    
    console.log("Worker: Computation done, sending result back.");
    postMessage({ result: sum, done: true });
};
