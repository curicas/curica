try {
    var s = "foo.md";
    var r = s.replace(".md", "");
    console.log(r);
} catch (e) {
    console.log("replace: " + (e.message || e));
}
