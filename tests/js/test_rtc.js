var fs = require('fs');

function myTrim(s) {
    if (!s) return "";
    var start = 0;
    while(start < s.length && (s[start] === " " || s[start] === "\t" || s[start] === String.fromCharCode(13) || s[start] === String.fromCharCode(10))) start++;
    var end = s.length - 1;
    while(end > start && (s[end] === " " || s[end] === "\t" || s[end] === String.fromCharCode(13) || s[end] === String.fromCharCode(10))) end--;
    return s.substring(start, end + 1);
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
                if (kStr.indexOf("**") > -1) kStr = kStr.substring(0, kStr.indexOf("**"));
                var key = myTrim(kStr);
                var val = myTrim(line.substring(colon + 1));
                meta[key] = val;
            }
        }
    }
    return meta;
}
try {
    var catPath = "./references/development/features/proposed/Real_Time_Communications";
    var files = fs.readdirSync(catPath);
    for (var j = 0; j < files.length; j = j + 1) {
        var f = files[j];
        if (f.indexOf(".md") > -1) {
            console.log("Reading " + f);
            var content = fs.readFileSync(catPath + "/" + f, "utf8");
            var meta = extractMetadata(content);
            console.log("OK");
        }
    }
} catch (e) {
    console.log("error: " + (e.message || e));
}
