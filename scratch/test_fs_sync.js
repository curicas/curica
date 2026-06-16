const fs = require('fs');

const path = "scratch/test_fd.txt";
const fd = fs.openSync(path, "w+");
console.log("Opened fd:", fd);

const written = fs.writeSync(fd, "hello fd sync");
console.log("Bytes written:", written);

const stats = fs.fstatSync(fd);
console.log("File size:", stats.size);

const buf = Buffer.alloc(20);
const read = fs.readSync(fd, buf, 0, 13, 0);
console.log("Bytes read:", read);
console.log("Data read:", buf.toString('utf8', 0, read));

fs.closeSync(fd);
console.log("Closed.");
