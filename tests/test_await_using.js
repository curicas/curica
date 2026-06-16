function AsyncResource(id) {
    var obj = {};
    obj.id = id;
    obj[Symbol.asyncDispose] = async function() {
        print("Disposing async resource: " + id);
        return 1; // Simulated promise
    };
    return obj;
}

async function test() {
    print("start");
    await using x = AsyncResource("A");
    await using y = AsyncResource("B");
    print("doing work");
}

var p = test();
print("done starting test");
