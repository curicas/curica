function Resource(id) {
    var obj = {};
    obj.id = id;
    obj[Symbol.dispose] = function() {
        print("Disposed resource: " + id);
    };
    return obj;
}

function test() {
    print("start");
    using x = Resource("A");
    using y = Resource("B");
    print("doing work");
}

test();
print("done");
