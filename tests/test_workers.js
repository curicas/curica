// test_workers.js
const fs = require('fs');

console.log("Main: Spawning Worker...");
var worker = new Worker('tests/test_worker_child.js');

worker.onmessage = function(msg) {
    console.log("Main: Received message from worker:", msg);
    if (msg.done) {
        console.log("Main: Test complete! Terminating worker...");
        worker.terminate();
    }
};

console.log("Main: Sending greeting to worker...");
worker.postMessage({ greeting: "Hello from Main Thread!" });
