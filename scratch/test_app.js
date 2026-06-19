console.log("Loading modules");
var net = require('net');
var fs = require('fs');
var cp = require('child_process');
console.log("Creating server");
var server = net.createServer(function(socket) {});
console.log("Listening");
server.listen(36195, "127.0.0.1");
console.log("Done");
