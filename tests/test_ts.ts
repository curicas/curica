interface User {
    name: string;
    age: number;
}

type UserID = string | number;

function printUser(user: User, id: UserID) {
    console.log("User:", user.name, "ID:", id);
}

let u: User = { name: "John", age: 30 };
printUser(u, 123);
