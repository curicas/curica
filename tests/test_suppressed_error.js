function Resource() {
    var obj = {};
    obj[Symbol.dispose] = function() {
        throw new Error("Error in dispose");
    };
    return obj;
}

function test() {
    try {
        using r = Resource();
        throw new Error("Primary Error");
    } catch (e) {
        print("Caught name: " + e.name);
        print("Caught message: '" + e.message + "'");
        if (e.error) {
            print("e.error: " + e.error.message);
        }
        if (e.suppressed) {
            print("e.suppressed: " + e.suppressed.message);
        }
    }
}
test();
