// test_fs.js — Verifies the synchronous fs module
const fs = require('fs');

// --- writeFileSync + readFileSync ---
const testPath = '/tmp/curica_test_fs.txt';
fs.writeFileSync(testPath, 'hello curica fs');
const content = fs.readFileSync(testPath, { encoding: 'utf8' });
console.log("readFileSync:", content); // "hello curica fs"

// --- existsSync ---
const existsTrue = fs.existsSync(testPath);
const existsFalse = fs.existsSync('/tmp/no_such_file_xyz');
console.log("existsSync (exists):", existsTrue);   // true
console.log("existsSync (missing):", existsFalse); // false

// --- appendFileSync ---
fs.appendFileSync(testPath, ' appended');
const content2 = fs.readFileSync(testPath, { encoding: 'utf8' });
console.log("after append:", content2); // "hello curica fs appended"

// --- readdirSync ---
const entries = fs.readdirSync('/tmp');
const entries_check = entries.length > 0;
console.log("readdirSync returns entries:", entries_check); // true

// --- statSync --- (isFile/isDirectory are boolean properties, not methods)
const statObj = fs.statSync(testPath);
console.log("statSync isFile:", statObj.isFile);        // true
console.log("statSync isDirectory:", statObj.isDirectory); // false
console.log("statSync size > 0:", statObj.size > 0);    // true

// --- copyFileSync + unlinkSync ---
const copyPath = '/tmp/curica_test_fs_copy.txt';
fs.copyFileSync(testPath, copyPath);
const copyExists = fs.existsSync(copyPath);
console.log("copy exists:", copyExists); // true
fs.unlinkSync(copyPath);
const afterUnlink = fs.existsSync(copyPath);
console.log("after unlink:", afterUnlink); // false

// --- mkdirSync ---
const dirPath = '/tmp/curica_test_dir';
fs.mkdirSync(dirPath);
const dirExists = fs.existsSync(dirPath);
console.log("mkdirSync dir exists:", dirExists); // true
fs.unlinkSync(testPath);

// --- SystemError on missing file ---
try {
    fs.readFileSync('/tmp/nonexistent_xyz_abc.txt');
} catch (err) {
    console.log("SystemError code:", err.code);    // ENOENT
    console.log("SystemError syscall:", err.syscall); // open
}

console.log("All fs tests passed!");

