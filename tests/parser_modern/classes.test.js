describe("Classes", () => {
    it("should declare a class with constructor and methods", () => {
        class Person {
            constructor(name) {
                this.name = name;
            }
            greet() {
                return "Hello " + this.name;
            }
        }
        const p = new Person("Alice");
        expect(p.name).toBe("Alice");
        expect(p.greet()).toBe("Hello Alice");
    });

    it("should support class inheritance with extends and super", () => {
        class Animal {
            constructor(type) {
                this.type = type;
            }
            sound() {
                return "generic sound";
            }
        }
        class Dog extends Animal {
            constructor() {
                super("dog");
            }
            sound() {
                return "woof";
            }
        }
        const d = new Dog();
        expect(d.type).toBe("dog");
        expect(d.sound()).toBe("woof");
    });

    it("should support static methods", () => {
        class MathHelper {
            static add(a, b) {
                return a + b;
            }
        }
        expect(MathHelper.add(5, 7)).toBe(12);
    });
});
