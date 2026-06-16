var kv = Curica.KV.open("test.db");

Curica.KV.set(kv, "user1", { name: "Alice", age: 30 });
Curica.KV.set(kv, "user2", { name: "Bob", age: 25 });

var u1 = Curica.KV.get(kv, "user1");
var u2 = Curica.KV.get(kv, "user2");

if (u1.name == "Alice" && u2.name == "Bob") {
    console.log("KV Store Set/Get successful");
} else {
    console.error("KV Store Set/Get failed");
}

Curica.KV.delete(kv, "user1");
var u1_deleted = Curica.KV.get(kv, "user1");
if (u1_deleted == null) {
    console.log("KV Store Delete successful");
} else {
    console.error("KV Store Delete failed");
}

console.log("Compacting KV Store...");
Curica.KV.compact(kv);

var u2_after_compaction = Curica.KV.get(kv, "user2");
if (u2_after_compaction && u2_after_compaction.name == "Bob") {
    console.log("KV Store Compaction successful");
} else {
    console.error("KV Store Compaction failed");
}
