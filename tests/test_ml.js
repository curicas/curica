const cp = require('child_process');

console.log('Spawning python.wasm child process...');

const py = cp.spawn('python', ['-c', 'print("Hello from Python WASM Child Process!")']);

py.stdout.on('data', function(chunk) {
    console.log('STDOUT FROM PYTHON WASM: ' + chunk.toString());
});

py.on('exit', function(code) {
    console.log('Python WASM Process exited with code: ' + code);
});

console.log('Spawn called, event loop running...');
