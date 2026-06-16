// test_async_fs.js — Verifies async fs callbacks via the thread pool
const fs = require('fs');

// --- fs.writeFile (async, callback) ---
fs.writeFile('/tmp/async_test.txt', 'async content', function(err) {
    if (err) {
        console.log("writeFile error:", err.message);
        return;
    }
    console.log("writeFile: ok");

    // --- fs.readFile (async, callback) ---
    fs.readFile('/tmp/async_test.txt', { encoding: 'utf8' }, function(err2, data) {
        if (err2) {
            console.log("readFile error:", err2.message);
            return;
        }
        console.log("readFile result:", data);

        // --- fs.stat (async, callback) ---
        fs.stat('/tmp/async_test.txt', function(err3, statObj) {
            if (err3) {
                console.log("stat error:", err3.message);
                return;
            }
            console.log("stat size > 0:", statObj.size > 0);

            // --- fs.unlink (async, callback) ---
            fs.unlink('/tmp/async_test.txt', function(err4) {
                if (err4) {
                    console.log("unlink error:", err4.message);
                } else {
                    console.log("unlink: ok");
                }
                console.log("All async fs tests passed!");
            });
        });
    });
});
