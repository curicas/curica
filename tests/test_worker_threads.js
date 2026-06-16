var wt = worker_threads;

console.log("Main thread isMainThread:", wt.isMainThread);

var worker = new wt.Worker("worker_script.js");
worker.on('message', function(msg) {
    console.log("Main thread received:", msg.hello);
    worker.postMessage({ ping: "ping from main" });
    
    setTimeout(function() {
        worker.terminate();
    }, 500);
});
console.log("Main thread finished synchronous setup");
console.log("worker.onmessage is falsy?", !worker.onmessage);
