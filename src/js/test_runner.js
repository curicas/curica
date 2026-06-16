const fs = require('fs');

let tests = [];
let currentDescribe = '';

globalThis.describe = function(name, fn) {
    const prevDescribe = currentDescribe;
    if (currentDescribe != '') {
        currentDescribe = currentDescribe + " > " + name;
    } else {
        currentDescribe = name;
    }
    fn();
    currentDescribe = prevDescribe;
};

globalThis.it = function(name, fn) {
    let testName = name;
    if (currentDescribe != '') {
        testName = currentDescribe + " > " + name;
    }
    let obj = {};
    obj.name = testName;
    obj.fn = fn;
    console.log("Registered test:", testName, "fn is:", typeof fn);
    tests.push(obj);
};

globalThis.test = globalThis.it;

globalThis.expect = function(actual) {
    let result = {};
    result.toBe = function(expected) {
        if (actual != expected) {
            throw new Error("Expected " + expected + " but received " + actual);
        }
    };
    result.toEqual = function(expected) {
        if (JSON.stringify(actual) != JSON.stringify(expected)) {
            throw new Error("Expected " + JSON.stringify(expected) + " but received " + JSON.stringify(actual));
        }
    };
    result.toBeTruthy = function() {
        if (actual == false) throw new Error("Expected truthy value but received " + actual);
        if (actual == null) throw new Error("Expected truthy value but received " + actual);
    };
    result.toBeFalsy = function() {
        if (actual != false && actual != null) throw new Error("Expected falsy value but received " + actual);
    };
    return result;
};

function findTests(dir) {
    try {
        const stats = fs.statSync(dir);
        if (!stats.isDirectory) {
            return [dir];
        }
    } catch (e) {
        return [dir];
    }

    let results = [];
    const files = fs.readdirSync(dir);
    for (let i = 0; i < files.length; i = i + 1) {
        const file = files[i];
        const stat = fs.statSync(dir + '/' + file);
        if (stat.isDirectory) {
            if (file != 'node_modules' && file != '.git') {
                let sub = findTests(dir + '/' + file);
                for (let k = 0; k < sub.length; k = k + 1) {
                    results.push(sub[k]);
                }
            }
        } else {
            let len = file.length;
            if ((len >= 8 && file.substring(len - 8) == '.test.js') ||
                (len >= 8 && file.substring(len - 8) == '.spec.js') ||
                (len >= 9 && file.substring(len - 9) == '.test.mjs') ||
                (len >= 9 && file.substring(len - 9) == '.spec.mjs')) {
                results.push(dir + '/' + file);
            }
        }
    }
    return results;
}

function runTests(targetDir) {
    const testFiles = findTests(targetDir);
    if (testFiles.length == 0) {
        console.log("No tests found.");
        return;
    }

    console.log("Running " + testFiles.length + " test suites...\n");

    let passed = 0;
    let failed = 0;

    for (let i = 0; i < testFiles.length; i = i + 1) {
        const file = testFiles[i];
        tests = [];
        currentDescribe = '';
        
        try {
            let path = file;
            if (path.startsWith('./')) {
                path = path.substring(2);
            }
            if (!path.startsWith('/')) {
                path = process.cwd() + '/' + path;
            }
            require(path);
        } catch (e) {
            console.log("X " + file + " - Suite failed to load");
            console.log("   " + (e.message || e));
            failed = failed + 1;
            continue;
        }

        for (let j = 0; j < tests.length; j = j + 1) {
            const t = tests[j];
            try {
                const res = t.fn();
                // Removed await res
                console.log("V " + t.name);
                passed = passed + 1;
            } catch (e) {
                console.log("X " + t.name);
                console.log("   " + (e.message || e));
                failed = failed + 1;
            }
        }
    }

    console.log("\nTest Summary:");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);
    if (failed > 0) {
        throw new Error("Tests failed");
    }
}

globalThis.__run_tests = function(targetDir) {
    try {
        runTests(targetDir);
    } catch(e) {
        console.log("Test Runner Error:", e);
        throw e;
    }
};
