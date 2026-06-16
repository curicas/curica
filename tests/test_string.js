let str = "Hello, World!";

console.log("=== Testing String Methods ===");

console.log("charAt(1): " + str.charAt(1));
console.log("charCodeAt(1): " + str.charCodeAt(1));
console.log("concat: " + str.concat(" How", " are", " you?"));
console.log("includes 'World': " + str.includes("World"));
console.log("indexOf 'o': " + str.indexOf("o"));
console.log("lastIndexOf 'o': " + str.lastIndexOf("o"));
console.log("startsWith 'Hello': " + str.startsWith("Hello"));
console.log("endsWith 'd!': " + str.endsWith("d!"));
console.log("slice(7, 12): " + str.slice(7, 12));
console.log("substring(7, 12): " + str.substring(7, 12));
console.log("toLowerCase: " + str.toLowerCase());
console.log("toUpperCase: " + str.toUpperCase());
console.log("repeat(3): " + "abc".repeat(3));
console.log("padEnd(5, '.'): " + "12".padEnd(5, "."));
console.log("padStart(5, '0'): " + "12".padStart(5, "0"));

let spaceStr = "   Trim Me   ";
console.log("trim: '" + spaceStr.trim() + "'");
console.log("trimStart: '" + spaceStr.trimStart() + "'");
console.log("trimEnd: '" + spaceStr.trimEnd() + "'");

console.log("replace 'World', 'Curica': " + str.replace("World", "Curica"));
console.log("replaceAll 'o', '0': " + str.replaceAll("o", "0"));

let splitArr = "a,b,c".split(",");
console.log("split length: " + splitArr.length);
console.log("split[0]: " + splitArr[0]);
console.log("split[2]: " + splitArr[2]);

console.log("String.fromCharCode: " + String.fromCharCode(72, 101, 108, 108, 111));

console.log("=== End String Test ===");
