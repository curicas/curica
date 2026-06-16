async function test() {
    console.log("Before await");
    let p = new Promise(function(resolve) {
        setTimeout(resolve, 100);
    });
    await p;
    console.log("After await");
}
test();
