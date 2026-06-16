describe("Generators", () => {
    it("should define and evaluate a generator function", () => {
        function* countToThree() {
            yield 1;
            yield 2;
            yield 3;
        }
        const gen = countToThree();
        const first = gen.next();
        expect(first.value).toBe(1);
        expect(first.done).toBe(false);
        
        const second = gen.next();
        expect(second.value).toBe(2);
        
        const third = gen.next();
        expect(third.value).toBe(3);
        
        const fourth = gen.next();
        expect(fourth.done).toBe(true);
    });

    it("should support for...of with generators", () => {
        function* generateLetters() {
            yield 'a';
            yield 'b';
            yield 'c';
        }
        let result = "";
        for (const letter of generateLetters()) {
            result = result + letter;
        }
        expect(result).toBe("abc");
    });
});
