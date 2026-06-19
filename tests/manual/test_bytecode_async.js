var fs = require('fs');
async function main() {
    var dir = "./";
    var fn = fs.existsSync;
    var ex = fn(dir);
}
main().catch(function(e) { console.error("Caught:", e); });
