const url = require('url');
console.log("url:", url);
console.log("url.parse:", url.parse);
var p = url.parse("/");
console.log("parsed:", p.pathname, p.path);
