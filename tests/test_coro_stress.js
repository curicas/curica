async function inner() {
    print("inner start");
    return 42;
}

async function test() {
    print("test start");
    var x = await inner();
    print("test end, x = " + x);
}

var p = test();
print("main sync end");
