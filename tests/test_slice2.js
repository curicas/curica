function test() {
    var args = Array.prototype.slice.call(arguments, 1);
    console.log("args len:", args.length);
    console.log("args:", args[0], args[1]);
}
test(1, 2, 3);
