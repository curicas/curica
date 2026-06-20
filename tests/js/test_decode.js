try {
    console.log(typeof decodeURIComponent);
    console.log(decodeURIComponent("foo"));
} catch (e) {
    console.log("error: " + (e.message || e));
}
