describe("Destructuring", () => {
    it("should destructure arrays", () => {
        const arr = [10, 20, 30];
        const [a, b, c] = arr;
        expect(a).toBe(10);
        expect(b).toBe(20);
        expect(c).toBe(30);
    });

    it("should handle array spread", () => {
        const arr = [1, 2, 3, 4];
        const [first, ...rest] = arr;
        expect(first).toBe(1);
        expect(rest.length).toBe(3);
        expect(rest[0]).toBe(2);
    });

    it("should destructure objects", () => {
        const obj = { x: 100, y: 200 };
        const { x, y } = obj;
        expect(x).toBe(100);
        expect(y).toBe(200);
    });

    it("should destructure objects with aliases", () => {
        const obj = { foo: 'bar' };
        const { foo: baz } = obj;
        expect(baz).toBe('bar');
    });
});
