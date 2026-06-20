try {
    var results = [];
    for(var i=0; i<1000; i++) {
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
    console.log("err: " + (e.message || e));
}
