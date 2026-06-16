describe("Modern Operators", () => {
    it("should support optional chaining", () => {
        const obj = { a: { b: 42 } };
        expect(obj?.a?.b).toBe(42);
        expect(obj?.x?.y).toBe(undefined);
    });

    it("should support nullish coalescing", () => {
        expect(null ?? "default").toBe("default");
        expect(undefined ?? "default").toBe("default");
        expect(0 ?? "default").toBe(0);
        expect("" ?? "default").toBe("");
    });

    it("should support exponentiation", () => {
        expect(2 ** 3).toBe(8);
        let x = 3;
        x **= 2;
        expect(x).toBe(9);
    });

    it("should support compound assignments and bitwise operators", () => {
        let a = 1;
        a += 2;
        expect(a).toBe(3);
        
        let b = 5; // 0101
        b &= 3; // 0011 -> 0001
        expect(b).toBe(1);
    });
});
