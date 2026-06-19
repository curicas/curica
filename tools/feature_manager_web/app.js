var net = require('net');
var fs = require('fs');
var cp = require('child_process');

var dirname = __dirname;

function sendResponse(socket, status, contentType, data) {
    var n = String.fromCharCode(13) + String.fromCharCode(10);
    var res = "HTTP/1.1 " + status + " OK" + n;
    res = res + "Access-Control-Allow-Origin: *" + n;
    res = res + "Content-Type: " + contentType + n;
    res = res + "Content-Length: " + data.length + n + n;
    res = res + data;
    socket.write(res);
    socket.end();
}



function send404(socket) {
    var n = String.fromCharCode(13) + String.fromCharCode(10);
    var res = "HTTP/1.1 404 Not Found" + n + "Content-Length: 9" + n + n + "Not Found";
    socket.write(res);
    socket.end();
}

var FEATURES_DIR = "./references/development/features";
var PROPOSED_DIR = FEATURES_DIR + "/proposed";
var APPROVED_DIR = FEATURES_DIR + "/approved";
var COMPLETED_DIR = FEATURES_DIR + "/completed";

function ensureDir(dir) {
    if (!fs.existsSync(dir)) {
        try { fs.mkdirSync(dir); } catch(e) {}
    }
}

function getFeatures(dirPath, status) {
    console.log("getFeatures called for", dirPath);
    var str = "";
    if (!fs.existsSync(dirPath)) {
        console.log("Dir does not exist", dirPath);
        return str;
    }
    
    console.log("Reading dir", dirPath);
    var cats = fs.readdirSync(dirPath);
    var first = true;
    for (var i = 0; i < cats.length; i = i + 1) {
        var cat = cats[i];
        var catPath = dirPath + "/" + cat;
        var stat = fs.statSync(catPath);
        if (stat.isDirectory) {
            var files = fs.readdirSync(catPath);
            for (var j = 0; j < files.length; j = j + 1) {
                var f = files[j];
                if (f.indexOf(".md") > -1) {
                    if (!first) str += ",";
                    first = false;
                    var name = f.substring(0, f.length - 3);
                    var fpath = catPath + "/" + f;
                    str += '{"name":"' + name + '","category":"' + cat + '","status":"' + status + '","file":"' + f + '","path":"' + fpath + '","metadata":{}}';
                }
            }
        }
    }
    return str;
}

function extractMetadata(content) {
    var meta = {};
    var lines = content.split(String.fromCharCode(10));
    for (var i = 0; i < lines.length; i = i + 1) {
        var line = lines[i];
        if (line.indexOf("**") === 0) {
            var colon = line.indexOf(":");
            if (colon > -1) {
                var kStr = line.substring(2, colon);
                if (kStr.indexOf("**") > -1) {
                    kStr = kStr.substring(0, kStr.indexOf("**"));
                }
                var key = kStr.trim();
                var val = line.substring(colon + 1).trim();
                meta[key] = val;
            }
        }
    }
    return meta;
}

function handleRequest(socket, method, url, body) {
    try {
        if (method === "GET" && url.indexOf("/styles.css") > -1) {
            var css = fs.readFileSync(dirname + "/ui/styles.css", "utf8");
            sendResponse(socket, 200, "text/css", css);
            return;
        }
        if (method === "GET" && url.indexOf("/frontend.js") > -1) {
            var js = fs.readFileSync(dirname + "/ui/frontend.js", "utf8");
            sendResponse(socket, 200, "application/javascript", js);
            return;
        }
        if (method === "GET" && (url === "/" || url.indexOf("/?") === 0)) {
            var html = fs.readFileSync(dirname + "/ui/index.html", "utf8");
            sendResponse(socket, 200, "text/html", html);
            return;
        }
        
        if (method === "GET" && url.indexOf("/api/features") > -1) {
            console.log("Handling /api/features");
            var features = "[";
            console.log("Calling getFeatures PROPOSED");
            var p = getFeatures(PROPOSED_DIR, "proposed");
            features += p;
            console.log("Calling getFeatures APPROVED");
            var a = getFeatures(APPROVED_DIR, "approved");
            if (a.length > 0) features += (p.length > 0 ? "," : "") + a;
            console.log("Calling getFeatures COMPLETED");
            var c = getFeatures(COMPLETED_DIR, "completed");
            if (c.length > 0) features += ((p.length > 0 || a.length > 0) ? "," : "") + c;
            features += "]";
            console.log("Done getting features. Sending...");
            sendResponse(socket, 200, "application/json", features);
            console.log("Done handling /api/features");
            return;
        }
        
        if (method === "GET" && url.indexOf("/api/feature_content") > -1) {
            var qIdx = url.indexOf("?path=");
            if (qIdx > -1) {
                var p = url.substring(qIdx + 6);
                p = decodeURIComponent(p);
                var content = fs.readFileSync(p, "utf8");
                sendResponse(socket, 200, "text/plain", content);
                return;
            }
        }
        
        if (method === "POST" && url.indexOf("/api/execute") > -1) {
            var payload = JSON.parse(body);
            try {
                var out = cp.execSync(payload.command);
                out = "" + out;
                sendResponse(socket, 200, "application/json", JSON.stringify({ output: out }));
            } catch (err) {
                var errOut = "" + err.message;
                sendResponse(socket, 200, "application/json", JSON.stringify({ error: errOut }));
            }
            return;
        }
        
        if (method === "POST" && url.indexOf("/api/move") > -1) {
            var payload2 = JSON.parse(body);
            var src = payload2.path;
            var destDir = "";
            if (payload2.status === "approved") destDir = APPROVED_DIR;
            if (payload2.status === "completed") destDir = COMPLETED_DIR;
            
            var tCatDir = destDir + "/" + payload2.category;
            ensureDir(tCatDir);
            var dest = tCatDir + "/" + payload2.file;
            
            fs.renameSync(src, dest);
            sendResponse(socket, 200, "application/json", JSON.stringify({ success: true }));
            return;
        }
        
        send404(socket);
    } catch (e) {
        var errStr = "" + (e.message || e);
        var n = String.fromCharCode(13) + String.fromCharCode(10);
        socket.write("HTTP/1.1 500 Internal Server Error" + n + "Content-Length: " + errStr.length + n + n + errStr);
        socket.end();
    }
}

var server = net.createServer(function(socket) {
    var buffer = "";
    socket.on('data', function(chunk) {
        buffer = buffer + chunk;
        
        var parts = buffer.split(" ");
        if (parts.length < 2) return;
        var method = myTrim(parts[0]);
        var url = myTrim(parts[1]);
        
        console.log("Parsed method: '" + method + "', url: '" + url + "'");
        
        handleRequest(socket, method, url, buffer);
    });
    
    socket.on('error', function(err) {
        console.log("Socket error: " + err);
    });
});

ensureDir(PROPOSED_DIR);
ensureDir(APPROVED_DIR);
ensureDir(COMPLETED_DIR);

server.listen(36195);
console.log("Feature Manager Web UI listening on http://127.0.0.1:36195");
