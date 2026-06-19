try {
    var results = [];
    for(var i=0; i<100; i++) {
        results.push({
            name: "test",
            category: "cat",
            status: "proposed",
            file: "f",
            path: "p",
            metadata: {}
        });
    }
    console.log(results.length);
} catch(e) {
    console.log(e.message || e);
}
