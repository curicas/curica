function test() { console.log("Test called!", this.name); }
test.call({name: "BoundObject"});
