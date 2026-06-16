// tests/test_fetch.js

console.log("Testing fetch() on https://example.com/");

var p1 = fetch("https://example.com/");

p1.then(function(response) {
    console.log("Status:", response.status);
    console.log("OK:", response.ok);
    response.text().then(function(text) {
        console.log("Response text length:", text.length);
        if (text.length > 0) {
            console.log("Test Passed!");
        }
    });
});

p1.catch(function(err) {
    console.log("Fetch failed:", err);
});
