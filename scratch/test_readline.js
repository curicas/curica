const readline = require('readline');
console.log('Testing Readline. isTTY:', process.stdin.isTTY);

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

rl.question('What is your name? ', (name) => {
    console.log(`Hello, ${name}!`);
    rl.close();
});
