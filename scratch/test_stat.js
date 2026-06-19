try {
    var fs = require('fs');
    var s = fs.statSync("./references/development/features/proposed");
    console.log(typeof s.isDirectory);
    console.log(s.isDirectory());
} catch (e) {
    console.log("stat: " + (e.message || e));
}
