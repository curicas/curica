function myTrim(s) {
    if (!s) return "";
    var start = 0;
    while(start < s.length && (s.charCodeAt(start) <= 32)) start++;
    var end = s.length - 1;
    while(end > start && (s.charCodeAt(end) <= 32)) end--;
    return s.substring(start, end + 1);
}
var m = myTrim("GET");
console.log(m.length);
for (var i = 0; i < m.length; i++) console.log(m.charCodeAt(i));
