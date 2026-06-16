const zlib = require('zlib');

const str = "Hello, this is a test string to be compressed. It has some repeating data, repeating data, repeating data.";
const buf = Buffer.from(str, 'utf8');

console.log("Original string:", str);
console.log("Original length:", buf.length);

zlib.deflate(buf, (err, compressed) => {
    if (err) {
        console.log("Deflate error:", err);
        return;
    }
    console.log("Compressed length:", compressed.length);
    
    zlib.inflate(compressed, (err, decompressed) => {
        if (err) {
            console.log("Inflate error:", err);
            return;
        }
        
        const resStr = decompressed.toString('utf8');
        console.log("Decompressed string:", resStr);
        console.log("Match:", str === resStr);
    });
});
