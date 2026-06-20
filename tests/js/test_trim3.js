function myTrim(s) {
    var start = 0;
    while(start < s.length && (s.charCodeAt(start) <= 32)) start++;
    var end = s.length - 1;
    while(end > start && (s.charCodeAt(end) <= 32)) end--;
    console.log("start: " + start + ", end: " + end);
    return s.substring(start, end + 1);
}
myTrim("GET");
