interface Person {
  name: string;
  age: number;
}type ID = string | number;
function sayHello(person: Person, id: ID) {
  console.log("Hello, " + person.name);
}const p: Person = {
  name: "Alice", age: 30
};
sayHello(p, 123);
