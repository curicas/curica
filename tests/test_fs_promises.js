const fs = require('fs');

async function runTests() {
    console.log("--- Testing fs.promises ---");

    const testFile = "/tmp/curica_promises_test.txt";
    
    try {
        console.log("1. Writing file...");
        await fs.promises.writeFile(testFile, "Hello Promises!");
        console.log("   writeFile: OK");

        console.log("2. Stat file...");
        const stats = await fs.promises.stat(testFile);
        console.log("   stat: OK (size: " + stats.size + ")");

        console.log("3. Reading file...");
        const data = await fs.promises.readFile(testFile, { encoding: "utf8" });
        console.log("   readFile: OK (content: '" + data + "')");
        if (data != "Hello Promises!") {
            throw new Error("Content mismatch!");
        }

        console.log("4. Appending file...");
        await fs.promises.appendFile(testFile, " Appended!");
        const data2 = await fs.promises.readFile(testFile, { encoding: "utf8" });
        console.log("   appendFile: OK (content: '" + data2 + "')");

        console.log("5. Unlink file...");
        await fs.promises.unlink(testFile);
        console.log("   unlink: OK");

        console.log("6. Testing rejection...");
        try {
            await fs.promises.stat("/tmp/curica_non_existent_file_12345.txt");
            console.log("   FAIL: stat should have rejected!");
        } catch (e) {
            console.log("   rejection: OK (caught: " + e.message + ")");
        }

        console.log("All fs.promises tests passed!");
    } catch (e) {
        console.log("Test Failed!", e.message);
    }
}

runTests();
