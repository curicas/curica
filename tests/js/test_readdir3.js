var fs = require('fs');
var cats = fs.readdirSync("./references/development/features/proposed");
console.log("length: " + cats.length);
for (var i = 0; i < cats.length; i++) {
    console.log(i + ": " + cats[i]);
}
