const fs = require('fs');
const entries = fs.readdirSync('/tmp');
console.log(typeof entries);
console.log(Array.isArray(entries));
