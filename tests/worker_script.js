var wt = worker_threads;

console.log("Worker started, isMainThread:", wt.isMainThread);

if (!wt.isMainThread) {
    console.log("Worker about to postMessage");
    wt.parentPort.postMessage({ hello: "from worker" });
    
    console.log("Worker about to call parentPort.on");
    wt.parentPort.on('message', function(msg) {
        console.log("Worker received ping:", msg.ping);
    });
    console.log("Worker finished synchronous setup");
}
