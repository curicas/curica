// scripts/fetch.js

var net = process.__require("net", ".");

function Headers(init) {
    this._map = {};
    if (init) {
        var keys = Object.keys(init);
        for (var i = 0; i < keys.length; i = i + 1) {
            this.append(keys[i], init[keys[i]]);
        }
    }
}

Headers.prototype.append = function(name, value) {
    var lowerName = name.toLowerCase();
    if (this._map[lowerName]) {
        this._map[lowerName] = this._map[lowerName] + ", " + value;
    } else {
        this._map[lowerName] = value;
    }
};

Headers.prototype.get = function(name) {
    return this._map[name.toLowerCase()] || null;
};

Headers.prototype.set = function(name, value) {
    this._map[name.toLowerCase()] = value;
};

Headers.prototype.has = function(name) {
    return !!this._map[name.toLowerCase()];
};

function Request(input, init) {
    init = init || {};
    if (input && input.url) {
        this.url = input.url;
        this.method = init.method || input.method;
        this.headers = new Headers(init.headers || input.headers);
        this.body = init.body || input.body;
    } else {
        this.url = input;
        this.method = init.method || "GET";
        this.headers = new Headers(init.headers);
        this.body = init.body || null;
    }
}

function Response(body, init) {
    init = init || {};
    this.body = body;
    this.status = init.status || 200;
    this.statusText = init.statusText || "OK";
    this.headers = new Headers(init.headers);
    this.ok = this.status >= 200 && this.status < 300;
}

Response.prototype.text = function() {
    var self = this;
    return new Promise(function(resolve) {
        resolve(self.body);
    });
};

Response.prototype.arrayBuffer = function() {
    var self = this;
    return new Promise(function(resolve) {
        resolve(self.body);
    });
};

Response.prototype.json = function() {
    var self = this;
    return new Promise(function(resolve) {
        resolve(JSON.parse(self.body));
    });
};

function parseUrl(urlStr) {
    var protocol = "http";
    var hostname = "";
    var port = 80;
    var path = "/";
    
    var idx = urlStr.indexOf("://");
    if (idx >= 0) {
        protocol = urlStr.substring(0, idx);
        urlStr = urlStr.substring(idx + 3);
    }
    
    var slashIdx = urlStr.indexOf("/");
    if (slashIdx >= 0) {
        path = urlStr.substring(slashIdx);
        urlStr = urlStr.substring(0, slashIdx);
    }
    
    var colonIdx = urlStr.indexOf(":");
    if (colonIdx >= 0) {
        var portStr = urlStr.substring(colonIdx + 1);
        port = parseInt(portStr, 10);
        hostname = urlStr.substring(0, colonIdx);
    } else {
        hostname = urlStr;
        if (protocol == "https") port = 443;
    }
    
    return {
        protocol: protocol,
        hostname: hostname,
        port: port,
        path: path
    };
}

function fetch(input, init) {
    var request = new Request(input, init);
    
    return new Promise(function(resolve, reject) {
        var url = parseUrl(request.url);
        if (url.protocol != "http" && url.protocol != "https") {
            reject(new TypeError("Unsupported protocol: " + url.protocol));
            return;
        }
        
        var port = url.port;
        if (!port) { 
            if (url.protocol == "https") {
                port = 443;
            } else {
                port = 80;
            }
        }
        
        var reqPath = "/";
        if (url.path) { reqPath = url.path; }
        var CRLF = String.fromCharCode(13, 10);
        var reqStr = request.method + " " + reqPath + " HTTP/1.1" + CRLF;
        reqStr = reqStr + "Host: " + url.hostname + CRLF;
        reqStr = reqStr + "Connection: close" + CRLF;
        
        var keys = Object.keys(request.headers._map);
        for (var i = 0; i < keys.length; i = i + 1) {
            reqStr = reqStr + keys[i] + ": " + request.headers._map[keys[i]] + CRLF;
        }
        
        if (request.body) {
            var bodyStr = request.body;
            if (!request.body.substring) {
                bodyStr = JSON.stringify(request.body);
            }
            reqStr = reqStr + "Content-Length: " + bodyStr.length + CRLF + CRLF;
            reqStr = reqStr + bodyStr;
        } else {
            reqStr = reqStr + CRLF;
        }
        
        var opts = {
            host: url.hostname,
            port: port,
            protocol: url.protocol,
            req_str: reqStr
        };
        
        __http.request(opts, function(err, status, statusText, headersMap, bodyStr) {
            if (err) {
                reject(new Error(err));
                return;
            }
            var headers = new Headers();
            var hkeys = Object.keys(headersMap);
            for (var j = 0; j < hkeys.length; j = j + 1) {
                headers.append(hkeys[j], headersMap[hkeys[j]]);
            }
            
            var redirect = 'follow';
            if (init && init.redirect) {
                redirect = init.redirect;
            }
            if ((status == 301 || status == 302 || status == 307 || status == 308) && redirect == 'follow') {
                var loc = headers.get('location');
                if (loc) {
                    if (loc.indexOf('://') < 0) {
                        var defaultPort = false;
                        if (url.protocol == "https" && url.port == 443) {
                            defaultPort = true;
                        }
                        if (url.protocol == "http" && url.port == 80) {
                            defaultPort = true;
                        }
                        var portStr = "";
                        if (!defaultPort) {
                            portStr = ":" + url.port;
                        }
                        loc = url.protocol + '://' + url.hostname + portStr + loc;
                    }
                    var newInit = init || {};
                    resolve(fetch(loc, newInit));
                    return;
                }
            }
            
            resolve(new Response(bodyStr, {
                status: status,
                statusText: statusText,
                headers: headers
            }));
        });
    });
}

globalThis.fetch = fetch;
globalThis.Headers = Headers;
globalThis.Request = Request;
globalThis.Response = Response;
