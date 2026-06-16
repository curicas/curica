const N = 10000000;
console.log("--- Testing Inline Caching Performance ---");

const obj = { x: 0, y: 1 };

for (let i = 0; i < N; i = i + 1) {
    obj.x = obj.x + obj.y;
}

console.log("obj.x =", obj.x);
