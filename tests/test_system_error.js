try {
    require("./does_not_exist.js");
} catch (e) {
    console.log("Caught error!");
    console.log("Name:", e.name);
    console.log("Code:", e.code);
    console.log("Syscall:", e.syscall);
    console.log("Message:", e.message);
}
