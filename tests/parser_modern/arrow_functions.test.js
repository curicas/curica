describe("Arrow Functions", () => {
    it("should evaluate concise body arrow functions", () => {
        const double = x => x * 2;
        expect(double(5)).toBe(10);
    });

    it("should evaluate block body arrow functions", () => {
        const add = (a, b) => {
            return a + b;
        };
        expect(add(10, 15)).toBe(25);
    });

    it("should bind lexical this", () => {
        const obj = {
            value: 42,
            getArrow: function() {
                return () => this.value;
            }
        };
        const arrow = obj.getArrow();
        expect(arrow()).toBe(42);
    });
});
