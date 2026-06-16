const http = require('http');
const os = require('os');
const crypto = require('crypto');
const url = require('url');

console.log("Starting Curica HTTP Server on " + os.platform() + " " + os.arch());

const server = http.createServer(function(req, res) {
    console.log("Request: " + req.method + " " + req.url);
    console.log("Calling url.parse...");
    var parsedUrl = url.parse(req.url);
    console.log("parsedUrl.path: " + parsedUrl.path);
    
    var n = "\n";
    console.log("Building response body...");
    var responseBody = "Hello from Curica!" + n;
    responseBody = responseBody + "Path: " + parsedUrl.path + n;
    console.log("Calling crypto.randomUUID...");
    var uuid = crypto.randomUUID();
    console.log("UUID: " + uuid);
    responseBody = responseBody + "Request ID: " + uuid + n;
    
    console.log("Calling res.end...");
    res.end(responseBody);
});

server.listen(8080, function() {
    console.log("Server listening on http://127.0.0.1:8080");
});
