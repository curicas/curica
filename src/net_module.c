/**
 * @file net_module.c
 * @brief TCP Networking Built-in Module (require('net')).
 *
 * Architecture:
 *   - net.createServer(onConnection) returns a Server JS object.
 *   - Each method (listen, address, close) is a native function whose
 *     `env` field holds a make_double((uintptr_t)ServerHandle*) so the
 *     C handler can recover the struct without depending on `this_val`.
 *   - Socket methods are similarly bound with SocketHandle* in env.
 *   - On POLLIN, data callbacks fire. On POLLOUT, connect completion fires.
 *   - All socket/server callbacks call vm_call_function directly (not via
 *     microtask queue) so they execute within the current el_run() tick.
 */
#include "net_module.h"
#include "alloc.h"
#include "event_loop.h"
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define ENFORCE_NET_ACCESS(vm) \
    if (!(vm)->allow_net) { \
        vm_throw_error((vm), create_error("PermissionError", create_string("Requires --allow-net access", 27))); \
        return VAL_UNDEFINED; \
    }

/* ── Helpers ────────────────────────────────────────────────────────────── */

static int make_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ── Handle Table ────────────────────────────────────────────────────────
 * Native methods need to recover their C struct pointer from f->env.
 * We cannot store a raw 64-bit pointer in a double (precision loss) or in
 * a GC-heap Value (GC may collect it before we assign it). Instead we
 * maintain a static table of C pointers indexed by a small integer, and
 * store that index (as a tagged integer Value) in f->env. Integers are
 * immediate values that never go through the GC heap.
 */
#define NET_HANDLE_TABLE_MAX 1024
static void* s_handle_table[NET_HANDLE_TABLE_MAX];
static int   s_handle_count = 0;

/** Register a C pointer and return its table index (as a JS integer Value). */
static Value register_handle(void* p) {
    if (s_handle_count >= NET_HANDLE_TABLE_MAX) return make_integer(-1);
    int id = s_handle_count++;
    s_handle_table[id] = p;
    return make_integer(id);
}

/** Retrieve the C pointer stored at the given table index. */
static void* lookup_handle(Value id_val) {
    if (!IS_INTEGER(id_val)) return NULL;
    int id = get_integer(id_val);
    if (id < 0 || id >= s_handle_count) return NULL;
    return s_handle_table[id];
}

/**
 * Create a native function whose env stores a handle-table ID.
 * No GC allocations happen during this call after the name string is created.
 */
static Value make_bound_fn(void* fn_ptr, const char* name, void* handle) {
    Value id = register_handle(handle);
    Value fn = create_native_function(fn_ptr, create_string(name, (int)strlen(name)));
    JSFunction* f = (JSFunction*)get_pointer(fn);
    f->env = id; /* immediate integer — GC-safe */
    return fn;
}

/** Recover the C handle from a bound function's env. */
static void* env_to_ptr(Value env_val) {
    return lookup_handle(env_val);
}

/* ── SocketHandle ───────────────────────────────────────────────────────── */

typedef struct SocketHandle {
    VM*      vm;
    int      fd;
    IOHandle io;
    Value    on_data;
    Value    on_close;
    Value    on_end;
    Value    on_connect;  /* fires on POLLOUT (non-blocking connect done) */
    char*    write_buf;
    size_t   write_len;
    size_t   write_off;
    int      end_pending;
} SocketHandle;

static void socket_flush_write(SocketHandle* s);

static void socket_io_cb(void* user_data, int revents) {
    SocketHandle* s = (SocketHandle*)user_data;

    /* ── Error or hangup — close socket ── */
    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) && !(revents & (POLLIN | POLLOUT))) {
        if (s->fd >= 0) { close(s->fd); s->fd = -1; }
        if (IS_POINTER(s->on_end)) {
            vm_call_function(s->vm, s->on_end, 0, NULL);
        }
        if (IS_POINTER(s->on_close)) {
            vm_call_function(s->vm, s->on_close, 0, NULL);
        }
        el_remove_io(g_event_loop, &s->io);
        return;
    }

    /* ── Connect completion (POLLOUT) ── */
    if (revents & POLLOUT) {
        if (IS_POINTER(s->on_connect)) {
            Value cb = s->on_connect;
            s->on_connect = VAL_NULL;
            s->io.events &= ~POLLOUT;
            s->io.events |= POLLIN;
            vm_call_function(s->vm, cb, 0, NULL);
        } else if (s->write_buf) {
            socket_flush_write(s);
        } else {
            /* No pending action for POLLOUT — stop watching it */
            s->io.events &= ~POLLOUT;
        }
    }

    /* ── Incoming data (POLLIN) ── */
    if (revents & POLLIN) {
        char buf[4096];
        ssize_t n = read(s->fd, buf, sizeof(buf));
        if (n > 0) {
            if (IS_POINTER(s->on_data)) {
                Value buffer = create_buffer_from_string(buf, (size_t)n, "utf8");
                Value argv[1] = { buffer };
                vm_call_function(s->vm, s->on_data, 1, argv);
            }
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            if (s->fd >= 0) { close(s->fd); s->fd = -1; }
            if (IS_POINTER(s->on_end)) {
                vm_call_function(s->vm, s->on_end, 0, NULL);
            }
            if (IS_POINTER(s->on_close)) {
                vm_call_function(s->vm, s->on_close, 0, NULL);
            }
            el_remove_io(g_event_loop, &s->io);
        }
    }
}

static void socket_flush_write(SocketHandle* s) {
    while (s->write_buf && s->write_off < s->write_len) {
        ssize_t n = write(s->fd,
                          s->write_buf + s->write_off,
                          s->write_len  - s->write_off);
        if (n > 0) {
            s->write_off += (size_t)n;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            s->io.events |= POLLOUT;
            return;
        } else {
            break;
        }
    }
    free(s->write_buf);
    s->write_buf = NULL;
    s->write_len = s->write_off = 0;
    s->io.events &= ~POLLOUT;
    if (s->end_pending && s->fd >= 0) {
        shutdown(s->fd, SHUT_WR);
        s->end_pending = 0;
    }
}

/* ── socket.on(event, cb) ───────────────────────────────────────────────── */

static Value js_socket_on(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    SocketHandle* s = (SocketHandle*)env_to_ptr(this_val);
     
    if (!s || arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* ev = (JSString*)get_pointer(args[0]);
     
    if (strcmp(ev->data, "data") == 0)    s->on_data    = args[1];
    else if (strcmp(ev->data, "close") == 0)  s->on_close   = args[1];
    else if (strcmp(ev->data, "end") == 0)    s->on_end     = args[1];
    else if (strcmp(ev->data, "connect") == 0) {
        s->on_connect = args[1];
    }
    /* Return a proxy object so chaining works */
    return VAL_UNDEFINED;
}

/* ── socket.write(buf) ──────────────────────────────────────────────────── */

static Value js_socket_write(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    SocketHandle* s = (SocketHandle*)env_to_ptr(this_val);
    if (!s || s->fd < 0 || arg_count < 1) return make_boolean(false);

    const char* data = NULL;
    size_t len = 0;
    if (IS_POINTER(args[0])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(args[0]);
            data = (const char*)buf->data; len = buf->length;
        } else if (h->obj_type == OBJ_STRING) {
            JSString* str = (JSString*)get_pointer(args[0]);
            data = str->data; len = str->length;
        }
    }
    if (!data || len == 0) return make_boolean(false);

    if (!s->write_buf) {
        ssize_t n = write(s->fd, data, len);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) n = 0;
        if (n < 0) return make_boolean(false);
        if (n < (ssize_t)len) {
            size_t rem = len - (size_t)n;
            s->write_buf = (char*)malloc(rem);
            memcpy(s->write_buf, data + n, rem);
            s->write_len = rem; s->write_off = 0;
            s->io.events |= POLLOUT;
        }
    } else {
        size_t new_len = s->write_len + len;
        char* new_buf = (char*)realloc(s->write_buf, new_len);
        memcpy(new_buf + s->write_len, data, len);
        s->write_buf = new_buf;
        s->write_len = new_len;
        s->io.events |= POLLOUT;
    }
    return make_boolean(true);
}

/* ── socket.end() ───────────────────────────────────────────────────────── */

static Value js_socket_end(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    SocketHandle* s = (SocketHandle*)env_to_ptr(this_val);
    if (s && s->fd >= 0) {
        if (s->write_buf && s->write_off < s->write_len) {
            s->end_pending = 1;
        } else {
            shutdown(s->fd, SHUT_WR);
        }
    }
    return VAL_UNDEFINED;
}

/* ── socket.destroy() ───────────────────────────────────────────────────── */

static Value js_socket_destroy(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    SocketHandle* s = (SocketHandle*)env_to_ptr(this_val);
    if (!s || s->fd < 0) return VAL_UNDEFINED;
    el_remove_io(g_event_loop, &s->io);
    close(s->fd); s->fd = -1;
    return VAL_UNDEFINED;
}

/** Build a JS Socket object, binding all methods with the handle in env. */
static Value make_socket_object(VM* vm, int fd) {
    SocketHandle* s = (SocketHandle*)calloc(1, sizeof(SocketHandle));
    s->vm = vm; s->fd = fd;
    s->on_data = s->on_close = s->on_connect = VAL_NULL;

    s->io.fd = fd; s->io.events = POLLIN;
    s->io.cb = socket_io_cb; s->io.user_data = s;
    el_add_io(g_event_loop, &s->io);

    Value obj = create_object();
    object_set(obj, create_string("on",      2), make_bound_fn(js_socket_on,      "on",      s));
    object_set(obj, create_string("write",   5), make_bound_fn(js_socket_write,   "write",   s));
    object_set(obj, create_string("end",     3), make_bound_fn(js_socket_end,     "end",     s));
    object_set(obj, create_string("destroy", 7), make_bound_fn(js_socket_destroy, "destroy", s));
    return obj;
}

/* ── ServerHandle ───────────────────────────────────────────────────────── */

typedef struct ServerHandle {
    VM*      vm;
    int      fd;
    IOHandle io;
    Value    on_connection;
    int      port;
} ServerHandle;

static void server_accept_cb(void* user_data, int revents) {
    ServerHandle* srv = (ServerHandle*)user_data;
    if (!(revents & POLLIN)) return;

    struct sockaddr_in peer; socklen_t peer_len = sizeof(peer);
    int conn_fd = accept(srv->fd, (struct sockaddr*)&peer, &peer_len);
    if (conn_fd < 0) return;
    make_nonblock(conn_fd);

    Value sock_obj = make_socket_object(srv->vm, conn_fd);

    char ip_str[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &peer.sin_addr, ip_str, sizeof(ip_str));
    object_set(sock_obj, create_string("remoteAddress", 13),
               create_string(ip_str, (int)strlen(ip_str)));
    object_set(sock_obj, create_string("remotePort", 10),
               make_integer(ntohs(peer.sin_port)));

    if (IS_POINTER(srv->on_connection)) {
        Value argv[1] = { sock_obj };
        vm_call_function(srv->vm, srv->on_connection, 1, argv);
    }
}

/* ── server.listen(port, cb) ────────────────────────────────────────────── */

static Value js_server_listen(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    ServerHandle* srv = (ServerHandle*)env_to_ptr(this_val);
    if (!srv || arg_count < 1) return VAL_UNDEFINED;

    int port = 0;
    if (IS_INTEGER(args[0])) port = get_integer(args[0]);
    else if (IS_DOUBLE(args[0])) port = (int)get_double(args[0]);

    Value on_listening = VAL_NULL;
    if (arg_count >= 2 && IS_POINTER(args[1])) on_listening = args[1];

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(srv->fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(srv->fd, 511);

    struct sockaddr_in actual; socklen_t alen = sizeof(actual);
    getsockname(srv->fd, (struct sockaddr*)&actual, &alen);
    srv->port = ntohs(actual.sin_port);

    srv->io.fd = srv->fd; srv->io.events = POLLIN;
    srv->io.cb = server_accept_cb; srv->io.user_data = srv;
    el_add_io(g_event_loop, &srv->io);

    /* Call listening callback synchronously — before el_run() blocks on poll */
    if (IS_POINTER(on_listening)) {
        vm_call_function(srv->vm, on_listening, 0, NULL);
    }
    return VAL_UNDEFINED;
}

/* ── server.address() ───────────────────────────────────────────────────── */

static Value js_server_address(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    ServerHandle* srv = (ServerHandle*)env_to_ptr(this_val);
    if (!srv) return VAL_NULL;
    Value addr = create_object();
    object_set(addr, create_string("port", 4), make_integer(srv->port));
    return addr;
}

/* ── server.close() ─────────────────────────────────────────────────────── */

static Value js_server_close(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    ServerHandle* srv = (ServerHandle*)env_to_ptr(this_val);
    if (!srv || srv->fd < 0) return VAL_UNDEFINED;
    el_remove_io(g_event_loop, &srv->io);
    close(srv->fd); srv->fd = -1;
    return VAL_UNDEFINED;
}

/* ── net.createServer(onConnection) ────────────────────────────────────── */

static Value js_net_create_server(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_NET_ACCESS(vm);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "socket", "net.createServer"));
        return VAL_UNDEFINED;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    make_nonblock(fd);

    ServerHandle* srv = (ServerHandle*)calloc(1, sizeof(ServerHandle));
    srv->vm = vm; srv->fd = fd;
    srv->on_connection = (arg_count >= 1) ? args[0] : VAL_NULL;

    Value obj = create_object();
    object_set(obj, create_string("listen",  6), make_bound_fn(js_server_listen,  "listen",  srv));
    object_set(obj, create_string("address", 7), make_bound_fn(js_server_address, "address", srv));
    object_set(obj, create_string("close",   5), make_bound_fn(js_server_close,   "close",   srv));
    return obj;
}

/* ── net.connect(port, host, onConnect) ─────────────────────────────────── */

static Value js_net_connect(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_NET_ACCESS(vm);
    if (arg_count < 1) return VAL_UNDEFINED;

    int port = 0;
    const char* host = "127.0.0.1";
    Value on_connect = VAL_NULL;

    if (IS_INTEGER(args[0])) port = get_integer(args[0]);
    else if (IS_DOUBLE(args[0])) port = (int)get_double(args[0]);

    if (arg_count >= 2 && IS_POINTER(args[1])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) host = ((JSString*)get_pointer(args[1]))->data;
        else if (h->obj_type == OBJ_FUNCTION) on_connect = args[1];
    }
    if (arg_count >= 3 && IS_POINTER(args[2])) on_connect = args[2];

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "socket", "net.connect"));
        return VAL_UNDEFINED;
    }
    make_nonblock(fd);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    connect(fd, (struct sockaddr*)&addr, sizeof(addr)); /* EINPROGRESS expected */

    SocketHandle* s = (SocketHandle*)calloc(1, sizeof(SocketHandle));
    s->vm = vm; s->fd = fd;
    s->on_data = s->on_close = VAL_NULL;
    s->on_connect = on_connect;

    /* Watch POLLOUT for connect completion; POLLIN added after connect fires */
    s->io.fd = fd; s->io.events = POLLOUT | POLLIN;
    s->io.cb = socket_io_cb; s->io.user_data = s;
    el_add_io(g_event_loop, &s->io);

    Value obj = create_object();
    object_set(obj, create_string("on",      2), make_bound_fn(js_socket_on,      "on",      s));
    object_set(obj, create_string("write",   5), make_bound_fn(js_socket_write,   "write",   s));
    object_set(obj, create_string("end",     3), make_bound_fn(js_socket_end,     "end",     s));
    object_set(obj, create_string("destroy", 7), make_bound_fn(js_socket_destroy, "destroy", s));
    return obj;
}


/* ── net._createSocketFromFd(fd) ────────────────────────────────────────── */

static Value js_net_create_socket_from_fd(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_INTEGER(args[0])) return VAL_UNDEFINED;
    int fd = get_integer(args[0]);
    make_nonblock(fd);
    return make_socket_object(vm, fd);
}

/* ── Module factory ─────────────────────────────────────────────────────── */


Value build_net_module(VM* vm) {
    (void)vm;
    Value exports = create_object();
    object_set(exports, create_string("createServer", 12),
               create_native_function((void*)js_net_create_server,
                                      create_string("createServer", 12)));
    object_set(exports, create_string("connect", 7),
               create_native_function((void*)js_net_connect,
                                      create_string("connect", 7)));
    object_set(exports, create_string("_createSocketFromFd", 19),
               create_native_function((void*)js_net_create_socket_from_fd,
                                      create_string("_createSocketFromFd", 19)));
    return exports;
}

void net_mark_gc_roots(GCTraceFn trace) {
    if (!g_event_loop) return;
    
    for (IOHandle* h = g_event_loop->io_handles; h; h = h->next) {
        if (h->cb == socket_io_cb) {
            SocketHandle* s = (SocketHandle*)h->user_data;
            trace(&s->on_data);
            trace(&s->on_close);
            trace(&s->on_end);
            trace(&s->on_connect);
        } else if (h->cb == server_accept_cb) {
            ServerHandle* srv = (ServerHandle*)h->user_data;
            trace(&srv->on_connection);
        }
    }
}

