const fs = require('fs');

let content = fs.readFileSync('src/fs_module.c', 'utf8');

// Insert forward declaration
content = content.replace(
    'static Value js_fs_open_sync(VM* vm, Value this_val, int arg_count, Value* args) {',
    'static Value build_stats_object(const struct stat* st);\n\nstatic Value js_fs_open_sync(VM* vm, Value this_val, int arg_count, Value* args) {'
);

fs.writeFileSync('src/fs_module.c', content);
console.log('Fixed implicit declaration');
