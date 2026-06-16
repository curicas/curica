const fs = require('fs');

async function runTests() {
    console.log("--- Testing fs.promises ---");
    const testFile = "/tmp/curica_promises_test.txt";
    try {
        console.log("fs.promises =", fs.promises);
        console.log("fs.promises.writeFile =", fs.promises.writeFile);
        
        console.log("1. Writing file...");
        let p = fs.promises.writeFile(testFile, "Hello Promises!");
        console.log("Promise returned =", p);
        
        await p;
        console.log("   writeFile: OK");
    } catch (e) {
        console.log("Test Failed!", e.name, e.message);
    }
    console.log("End of runTests");
}

runTests();
console.log("End of script");
