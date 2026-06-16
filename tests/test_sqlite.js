const fs = require('fs');

console.log("Testing SQLite Database...");

// Create a new database file
const db = new Database('test.db');
console.log("Database opened.");

db.exec("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");
db.exec("DELETE FROM users");

console.log("Table created and cleared.");

const insertStmt = db.prepare("INSERT INTO users (name, age) VALUES (?, ?)");
const res1 = insertStmt.run("Alice", 30);
console.log("Inserted Alice, lastRowId:", res1.lastInsertRowid);
const res2 = insertStmt.run("Bob", 25);
console.log("Inserted Bob, lastRowId:", res2.lastInsertRowid);
insertStmt.finalize();

var selectStmt = db.prepare("SELECT * FROM users WHERE age > ?");
var rows = selectStmt.all(20);
console.log("Users over 20 count:", rows.length);
for (var i = 0; i < rows.length; i = i + 1) {
    console.log(rows[i].name, rows[i].age);
}

var singleRow = selectStmt.get(28);
console.log("User over 28 name:", singleRow.name);

selectStmt.finalize();
db.close();

console.log("Test Passed!");
