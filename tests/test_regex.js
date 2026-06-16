var rx = new RegExp("^(http|https):\\/\\/([^\\/:]+)(:\\d+)?(\\/.*)?$");
var str = "http://httpbin.org/get";
var match = rx.exec(str);
console.log("match is:", match);
