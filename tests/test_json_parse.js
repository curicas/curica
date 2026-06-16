const fs = require('fs');
const env = JSON.parse(fs.readFileSync('curica.env.json', 'utf8'));
console.log("env.tools:", env.tools);
console.log("keys:", Object.keys(env.tools).join(', '));
