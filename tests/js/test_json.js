try {
    var arr = [{a: 1}, {b: 2}];
    console.log(JSON.stringify(arr));
} catch(e) {
    console.log("error: " + (e.message || e));
}
