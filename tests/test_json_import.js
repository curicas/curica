import config from "./test_data.json" with { type: "json" };

print("=== JSON Module Import Test ===");
print("config.name: " + config.name);
print("config.version: " + config.version);
print("config.stable: " + config.stable);
print("config.tags[1]: " + config.tags[1]);
print("config.config.port: " + config.config.port);
print("config.config.debug: " + config.config.debug);
