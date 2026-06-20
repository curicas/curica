const asyncIterable = {};
asyncIterable[Symbol.asyncIterator] = function() {
    let i = 0;
    console.log("Called asyncIterator method!");
    return {
        next: async function() {
            console.log("Called next!");
            if (i < 3) {
                return { value: i++, done: false };
            }
            return { value: undefined, done: true };
        }
    };
};

async function test() {
    let sum = 0;
    console.log("Before loop");
    for await (const val of asyncIterable) {
        console.log("Got:", val);
        sum += val;
    }
    console.log("Sum:", sum);
}

test();
