const crypto = require('crypto');
console.log('--- Testing Crypto ---');

const uuid = crypto.randomUUID();
console.log('UUID:', uuid);
if (uuid.length != 36) throw new Error('Invalid UUID length');

const bytes = crypto.randomBytes(16);
var hexBytes = [];
var hexChars = '0123456789abcdef';
for (var i = 0; i < bytes.length; i = i + 1) {
    var b = bytes[i];
    var b_high = b - (b % 16);
    b_high = b_high / 16;
    var b_low = b % 16;
    var h = hexChars[b_high] + hexChars[b_low];
    hexBytes.push(h);
}
var hexString = '';
for (var i = 0; i < hexBytes.length; i = i + 1) {
    hexString = hexString + hexBytes[i];
}
console.log('Random Bytes (Hex):', hexString);
if (bytes.length != 16) throw new Error('Invalid random bytes length');

const hash = crypto.createHash('sha256');
hash.update('hello world');
const digest = hash.digest('hex');
console.log('SHA256 of "hello world":', digest);

if (digest != 'b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9') {
    throw new Error('Invalid SHA256 digest');
}
console.log('Crypto tests passed!');
