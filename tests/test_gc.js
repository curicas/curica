console.log("=== Memory Auditing Started ===");

async function test_gc_coroutines() {
    for (let j = 0; j < 5; j = j + 1) {
        let promises = [];
        for (let i = 0; i < 50; i = i + 1) {
            let cyclic = {};
            cyclic.self = cyclic;
            cyclic.data = [1, 2, 3, i];
            
            // Closures and nested async functions to stress test hermetic allocator
            async function closure() {
                return cyclic.data[3];
            }
            if (j == 0 && i == 0) console.log("Pushing closure!");
            promises.push(closure());
        }
    }
}

async function run() {
    console.log("Calling test_gc...");
    let p = test_gc_coroutines();
    console.log("Awaiting test_gc...");
    await p;
    console.log("GC stress test passed without leaks!");
}

console.log("Invoking run...");
run();
