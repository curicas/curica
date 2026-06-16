console.log("--- Testing Moving Copying Generational GC ---");

let root_arr = [];

for (let i = 0; i < 50; i = i + 1) {
    let temp_arr = [];
    for (let j = 0; j < 10000; j = j + 1) {
        temp_arr.push({ id: j, data: "nursery_object_" + j });
    }
    // Only keep the last element to test write barriers and forwarding pointers
    root_arr.push(temp_arr[9999]);
}

console.log("Survived objects:", root_arr.length);
console.log("Last object data:", root_arr[49].data);
console.log("GC test completed successfully.");
