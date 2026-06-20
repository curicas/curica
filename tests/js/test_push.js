try {
    var arr = [];
    arr.push("hello");
    console.log(arr[0]);
} catch (e) {
    console.log(e.message || e);
}
