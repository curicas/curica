describe("Regex Literals", () => {
    it("should instantiate a RegExp from a literal", () => {
        const r = /hello/i;
        expect(r.test("HELLO world")).toBe(true);
        expect(r.test("goodbye")).toBe(false);
    });

    it("should support regex after operators", () => {
        const str = "test";
        const res = typeof str === 'string' && /test/.test(str);
        expect(res).toBe(true);
    });
});
