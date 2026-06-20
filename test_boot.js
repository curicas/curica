const os = require("os");
console.log("Hello from test_boot.js");
console.log("TEST_ENV_VAR:", os.getenv("TEST_ENV_VAR"));

async function testAsync() {
    console.log("testAsync 1");
    // const r = await fetch("https://example.com");
    // console.log(await r.text());
}
testAsync();
