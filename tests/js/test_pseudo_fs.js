const fs = require('fs');

console.log("Testing /proc/self/stat:");
const statData = fs.readFileSync("/proc/self/stat", "utf8");
console.log("-> " + statData.trim());

console.log("Testing /sys/cpu/count:");
const cpuData = fs.readFileSync("/sys/cpu/count", "utf8");
console.log("-> " + cpuData.trim());

console.log("Testing /sys/memory/limit:");
const memData = fs.readFileSync("/sys/memory/limit", "utf8");
console.log("-> " + memData.trim());

console.log("Testing VFS stat:");
const st = fs.statSync("/proc/self/stat");
console.log("-> size: " + st.size);
