var fs = require('fs');
var cats = fs.readdirSync("./references/development/features/proposed");
var idx = cats.indexOf("Advanced_Networking");
console.log("Next: " + cats[idx+1]);
