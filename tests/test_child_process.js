const cp = require('child_process');

console.log('--- Testing Child Process ---');
const EventEmitter = require('events');
console.log('EventEmitter:', EventEmitter);
console.log('EventEmitter.prototype:', EventEmitter.prototype);
console.log('EventEmitter.prototype.on:', EventEmitter.prototype.on);
const Readable = require('stream').Readable;
console.log('Readable.prototype.push:', Readable.prototype.push);
console.log('Readable.prototype.on:', Readable.prototype.on);

console.log('Spawning child...');
const child = cp.spawn('echo', ['hello', 'world']);
console.log('Spawned successfully, child pid:', child.pid);
console.log('child:', child);
console.log('child.stdout:', child.stdout);
console.log('child.stdout.on:', child.stdout.on);
console.log('child.stdout.readable:', child.stdout.readable);
console.log('child.stdout.push:', child.stdout.push);

let output = '';

child.stdout.on('data', function(chunk) {
    for (var i = 0; i < chunk.length; i = i + 1) {
        output = output + String.fromCharCode(chunk[i]);
    }
});

child.on('exit', function(code) {
    console.log('Child process exited with code:', code);
    console.log('Output:', JSON.stringify(output));
    if (!output.includes('hello world')) {
        console.error('Child process output does not match expected output');
        process.exit(1);
    }
    console.log('Child Process tests passed!');
});
