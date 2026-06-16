var p1 = fetch("http://httpbin.org/get");
p1.then(function(r) { return r.text(); }).then(console.log).catch(console.log);
