const fs = require('fs');

async function runGCStressTest() {
    console.log("--- Starting GC Stress Test on Thread Pool ---");

    const testFile = "/tmp/curica_gc_stress.txt";
    await fs.promises.writeFile(testFile, "Hello GC Stress!");

    // Start an asynchronous read operation
    const promise = fs.promises.readFile(testFile, { encoding: "utf8" });

    // While the background thread is reading the file, forcefully trigger the GC
    // multiple times to see if the pending Promise is incorrectly swept.
    console.log("Triggering tight loop to flood allocation and force GC sweeps...");
    for (let i = 0; i < 10000; i = i + 1) {
        let dummy = { a: i, b: "temp" };
    }

    // Wait for the promise
    try {
        const data = await promise;
        console.log("Pending promise resolved successfully after GC sweeps: " + data);
        if (data != "Hello GC Stress!") {
            throw new Error("Data mismatch!");
        }
        console.log("GC Stress Test PASSED!");
    } catch (e) {
        console.log("GC Stress Test FAILED: " + e.message);
    }
    
    await fs.promises.unlink(testFile);
}

runGCStressTest();
