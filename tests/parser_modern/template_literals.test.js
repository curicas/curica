describe("Template Literals", () => {
    it("should evaluate basic template strings", () => {
        const str = `hello world`;
        expect(str).toBe("hello world");
    });

    it("should interpolate expressions", () => {
        const x = 10;
        const y = 20;
        const str = `${x} + ${y} = ${x + y}`;
        expect(str).toBe("10 + 20 = 30");
    });

    it("should handle escape sequences", () => {
        const str = `line1\nline2`;
        expect(str.length).toBe(11);
        expect(str[5]).toBe("\n");
    });

    it("should handle nested template literals", () => {
        const isTrue = true;
        const str = `nested: ${isTrue ? `yes ${1}` : `no`}`;
        expect(str).toBe("nested: yes 1");
    });
});
