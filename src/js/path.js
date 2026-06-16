module.exports = {
    join: function(a, b, c, d) {
        var res = a;
        if (b) res = res + '/' + b;
        if (c) res = res + '/' + c;
        if (d) res = res + '/' + d;
        
        var parts = res.split('/');
        var final_parts = [];
        var final_len = 0;
        var starts_with_slash = false;
        
        for (var i = 0; i < parts.length; i = i + 1) {
            var p = parts[i];
            if (i == 0) {
                if (p == '') starts_with_slash = true;
            }
            if (p == '') continue;
            if (p == '.') continue;
            if (p == '..') {
                if (final_len > 0) {
                    final_len = final_len - 1;
                }
            } else {
                final_parts[final_len] = p;
                final_len = final_len + 1;
            }
        }
        
        var j = "";
        for (var k = 0; k < final_len; k = k + 1) {
            if (k > 0) j = j + '/';
            j = j + final_parts[k];
        }
        
        if (starts_with_slash) {
            return '/' + j;
        }
        return j;
    },
    basename: function(p) {
        var parts = p.split('/');
        return parts[parts.length - 1];
    }
};
