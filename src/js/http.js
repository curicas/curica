const net = require('net');

module.exports = {
    createServer: function(handler) {
        var server = net.createServer(function(socket) {
            socket.on('data', function(chunk) {
                var chunkStr = "" + chunk;
                
                var req = { method: "GET", url: "/", headers: {} };
                var parts = chunkStr.split(' ');
                if (parts.length > 1) {
                    req.method = parts[0];
                    req.url = parts[1];
                }
                
                var res = {
                    statusCode: 200,
                    headers: {},
                    setHeader: function(k, v) { this.headers[k] = v; },
                    writeHead: function(statusCode, headers) {
                        this.statusCode = statusCode;
                        if (headers) {
                            var keys = Object.keys(headers);
                            for (var i = 0; i < keys.length; i = i + 1) {
                                var k = keys[i];
                                this.headers[k] = headers[k];
                            }
                        }
                    },
                    end: function(data) {
                        var n = "\r\n";
                        var response = "HTTP/1.1 " + this.statusCode + " OK" + n;
                        this.headers["Content-Length"] = data.length;
                        this.headers["Connection"] = "close";
                        
                        var keys = Object.keys(this.headers);
                        for (var i = 0; i < keys.length; i = i + 1) {
                            var k = keys[i];
                            response = response + k + ": " + this.headers[k] + n;
                        }
                        response = response + n + data;
                        socket.write(response);
                        socket.end();
                    }
                };
                
                handler(req, res);
            });
        });
        return server;
    }
};
