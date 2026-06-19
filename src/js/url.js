function URLSearchParams(init) {
    this._params = [];
    if (init && init.split) {
        if (init.indexOf('?') == 0) init = init.slice(1);
        var pairs = init.split('&');
        for (var i = 0; i < pairs.length; i = i + 1) {
            if (!pairs[i]) continue;
            var idx = pairs[i].indexOf('=');
            if (idx == -1) {
                this._params.push([decodeURIComponent(pairs[i]), '']);
            } else {
                this._params.push([
                    decodeURIComponent(pairs[i].slice(0, idx)),
                    decodeURIComponent(pairs[i].slice(idx + 1))
                ]);
            }
        }
    }
}

URLSearchParams.prototype.append = function(name, value) {
    this._params.push([String(name), String(value)]);
};

URLSearchParams.prototype.delete = function(name) {
    var nameStr = String(name);
    var newParams = [];
    for (var i = 0; i < this._params.length; i = i + 1) {
        if (this._params[i][0] != nameStr) newParams.push(this._params[i]);
    }
    this._params = newParams;
};

URLSearchParams.prototype.get = function(name) {
    var nameStr = String(name);
    var param = this._params.find(function(p) { return p[0] == nameStr; });
    if (param) return param[1];
    return null;
};

URLSearchParams.prototype.getAll = function(name) {
    var nameStr = String(name);
    var result = [];
    for (var i = 0; i < this._params.length; i = i + 1) {
        if (this._params[i][0] == nameStr) {
            result.push(this._params[i][1]);
        }
    }
    return result;
};

URLSearchParams.prototype.has = function(name) {
    var nameStr = String(name);
    return this._params.some(function(p) { return p[0] == nameStr; });
};

URLSearchParams.prototype.set = function(name, value) {
    var nameStr = String(name);
    var valueStr = String(value);
    var found = false;
    for (var i = 0; i < this._params.length; i = i + 1) {
        if (this._params[i][0] == nameStr) {
            if (!found) {
                this._params[i][1] = valueStr;
                found = true;
            } else {
                this._params.splice(i, 1);
                i = i - 1;
            }
        }
    }
    if (!found) {
        this._params.push([nameStr, valueStr]);
    }
};

URLSearchParams.prototype.toString = function() {
    var arr = [];
    for (var i = 0; i < this._params.length; i = i + 1) {
        var str = encodeURIComponent(this._params[i][0]) + '=' + encodeURIComponent(this._params[i][1]);
        arr.push(str);
    }
    return arr.join('&');
};

function URL(url, base) {
    var fullUrl = url;
    if (base) {
        if (url.indexOf('http://') != 0 && url.indexOf('https://') != 0) {
            var baseEnds = (base[base.length - 1] == '/');
            var urlStarts = (url[0] == '/');
            if (baseEnds && urlStarts) {
                fullUrl = base + url.slice(1);
            } else if (!baseEnds && !urlStarts) {
                fullUrl = base + '/' + url;
            } else {
                fullUrl = base + url;
            }
        }
    }

    var protoIdx = fullUrl.indexOf('://');
    if (protoIdx == -1) throw new TypeError('Invalid URL');
    this.protocol = fullUrl.slice(0, protoIdx + 1);

    var rest = fullUrl.slice(protoIdx + 3);
    var pathIdx = rest.indexOf('/');
    var searchIdx = rest.indexOf('?');
    var hashIdx = rest.indexOf('#');

    var endHostIdx = rest.length;
    if (pathIdx != -1 && pathIdx < endHostIdx) endHostIdx = pathIdx;
    if (searchIdx != -1 && searchIdx < endHostIdx) endHostIdx = searchIdx;
    if (hashIdx != -1 && hashIdx < endHostIdx) endHostIdx = hashIdx;

    this.host = rest.slice(0, endHostIdx);
    
    var portIdx = this.host.indexOf(':');
    if (portIdx != -1) {
        this.hostname = this.host.slice(0, portIdx);
        this.port = this.host.slice(portIdx + 1);
    } else {
        this.hostname = this.host;
        this.port = '';
    }

    rest = rest.slice(endHostIdx);
    var searchStartIdx = rest.indexOf('?');
    var hashStartIdx = rest.indexOf('#');

    if (searchStartIdx == -1) {
        if (hashStartIdx == -1) {
            if (rest) this.pathname = rest; else this.pathname = '/';
            this.search = '';
            this.hash = '';
        } else {
            var p = rest.slice(0, hashStartIdx);
            if (p) this.pathname = p; else this.pathname = '/';
            this.search = '';
            this.hash = rest.slice(hashStartIdx);
        }
    } else {
        var p2 = rest.slice(0, searchStartIdx);
        if (p2) this.pathname = p2; else this.pathname = '/';
        if (hashStartIdx == -1) {
            this.search = rest.slice(searchStartIdx);
            this.hash = '';
        } else {
            this.search = rest.slice(searchStartIdx, hashStartIdx);
            this.hash = rest.slice(hashStartIdx);
        }
    }

    this.searchParams = new URLSearchParams(this.search);
}

URL.prototype.toString = function() {
    var str = this.protocol + '//' + this.host + this.pathname;
    var searchStr = this.searchParams.toString();
    if (searchStr) {
        str = str + '?' + searchStr;
    }
    if (this.hash) {
        str = str + this.hash;
    }
    return str;
};

module.exports = { 
    URL: URL, 
    URLSearchParams: URLSearchParams,
    parse: function(urlStr) {
        try {
            var u = new URL(urlStr, "http://localhost");
            u.path = u.pathname + u.search;
            return u;
        } catch (e) {
            return { pathname: urlStr, path: urlStr, search: "" };
        }
    }
};
