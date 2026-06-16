const fs = require('fs');

console.log("Reading from VFS readonly disk:");
const content = fs.readFileSync('/disk/mydata/hello.txt', 'utf8');
console.log(content);
