var enc = encodeURIComponent("hello world! @#$%^&*");
console.log("Encoded: " + enc);
if (enc !== "hello%20world!%20%40%23%24%25%5E%26*") {
    throw new Error("Encode failed: " + enc);
}

var dec = decodeURIComponent("hello%20world!%20%40%23%24%25%5E%26*");
console.log("Decoded: " + dec);
if (dec !== "hello world! @#$%^&*") {
    throw new Error("Decode failed: " + dec);
}

console.log("URL TESTS PASSED");
