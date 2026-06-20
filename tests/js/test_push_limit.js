try {
    var arr = [];
    for(var i=0; i<100; i++) arr.push(i);
    console.log(arr.length);
} catch(e) {
    console.log(e.message || e);
}
