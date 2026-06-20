const fs = require('fs');

let content = fs.readFileSync('src/net_module.c', 'utf8');

content = content.replace(
    'ssize_t n = recv(s->fd, buf, sizeof(buf), 0);',
    'ssize_t n = read(s->fd, buf, sizeof(buf));'
);

content = content.replace(
    'ssize_t n = send(s->fd,\n                         s->write_buf + s->write_off,\n                         s->write_len  - s->write_off, 0);',
    'ssize_t n = write(s->fd,\n                          s->write_buf + s->write_off,\n                          s->write_len  - s->write_off);'
);

content = content.replace(
    'ssize_t n = send(s->fd, data, len, 0);',
    'ssize_t n = write(s->fd, data, len);'
);

const new_func = `
/* ── net._createSocketFromFd(fd) ────────────────────────────────────────── */

static Value js_net_create_socket_from_fd(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_INTEGER(args[0])) return VAL_UNDEFINED;
    int fd = get_integer(args[0]);
    make_nonblock(fd);
    return make_socket_object(vm, fd);
}

/* ── Module factory ─────────────────────────────────────────────────────── */
`;

content = content.replace(
    '/* ── Module factory ─────────────────────────────────────────────────────── */',
    new_func
);

content = content.replace(
    'create_string("connect", 7)));',
    'create_string("connect", 7)));\n    object_set(exports, create_string("_createSocketFromFd", 19),\n               create_native_function((void*)js_net_create_socket_from_fd,\n                                      create_string("_createSocketFromFd", 19)));'
);

fs.writeFileSync('src/net_module.c', content);
console.log('patched net_module.c');
