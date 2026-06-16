describe("For Of / For In loops", () => {
    it("should iterate an array with for...of", () => {
        const arr = [10, 20, 30];
        let sum = 0;
        for (const val of arr) {
            sum += val;
        }
        expect(sum).toBe(60);
    });

    it("should iterate object keys with for...in", () => {
        const obj = { a: 1, b: 2 };
        let keys = "";
        for (const key in obj) {
            keys += key;
        }
        // object iteration order might not be strictly defined, but usually a, b
        expect(keys.length).toBe(2);
    });
});
