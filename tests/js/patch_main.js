const fs = require('fs');

let content = fs.readFileSync('src/main.c', 'utf8');

// We have EventLoop loop; el_init(&loop); appearing 4 times.
// We want to move it to right before VM vm; vm_init(&vm);

content = content.replace(
    '        VM vm;\n        vm_init(&vm);',
    '        EventLoop loop;\n        el_init(&loop);\n        VM vm;\n        vm_init(&vm);'
);
content = content.replace(
    '        EventLoop loop;\n        el_init(&loop);\n\n        extern unsigned char scripts_fetch_js[];',
    '        extern unsigned char scripts_fetch_js[];'
);
content = content.replace(
    '        EventLoop loop;\n        el_init(&loop);  /* Must come before script execution so g_event_loop is set */\n\n        extern unsigned char scripts_fetch_js[];',
    '        /* EventLoop was initialized before VM */\n        extern unsigned char scripts_fetch_js[];'
);

fs.writeFileSync('src/main.c', content);
