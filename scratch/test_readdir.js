var fs = require('fs');
var cats = fs.readdirSync("../../references/development/features/proposed");
console.log(cats.length);
