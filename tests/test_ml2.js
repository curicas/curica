const cp = require('child_process');
console.log('1');
const py = cp.spawn('python', ['-c', 'print("Hi")']);
console.log('2');
py.stdout.on('data', function(chunk) {
    console.log('3: ' + chunk);
});
console.log('4');
py.on('exit', function(code) {
    console.log('5: ' + code);
});
console.log('6');
