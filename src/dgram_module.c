#include "dgram_module.h"
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

/* ── Handle Table ──────────────────────────────────────────────────────── */

#define DGRAM_HANDLE_TABLE_MAX 1024
static void* s_handle_table[DGRAM_HANDLE_TABLE_MAX];
static int   s_handle_count = 0;

static Value register_handle(void* p) {
    if (s_handle_count >= DGRAM_HANDLE_TABLE_MAX) return make_integer(-1);
    int id = s_handle_count++;
    s_handle_table[id] = p;
    return make_integer(id);
}

static void* lookup_handle(Value id_val) {
    if (!IS_INTEGER(id_val)) return NULL;
    int id = get_integer(id_val);
    if (id < 0 || id >= s_handle_count) return NULL;
    return s_handle_table[id];
}

static Value make_bound_fn(void* fn_ptr, const char* name, void* handle) {
    Value id = register_handle(handle);
    Value fn = create_native_function(fn_ptr, create_string(name, (int)strlen(name)));
    JSFunction* f = (JSFunction*)get_pointer(fn);
    f->env = id;
    return fn;
}

static void* env_to_ptr(Value env_val) {
    return lookup_handle(env_val);
}

/* ── DgramSocketHandle ──────────────────────────────────────────────────── */

typedef struct DgramSocketHandle {
    VM*      vm;
    int      fd;
    IOHandle io;
    Value    on_message;
    Value    on_error;
    Value    on_close;
} DgramSocketHandle;

static void dgram_io_cb(void* user_data, int revents) {
    DgramSocketHandle* s = (DgramSocketHandle*)user_data;

    if ((revents & (POLLERR | POLLNVAL)) && !(revents & POLLIN)) {
        if (IS_POINTER(s->on_error)) {
            Value err = create_error("Error", create_string("Socket error", 12));
            Value argv[1] = { err };
            vm_call_function(s->vm, s->on_error, 1, argv);
        }
        return;
    }

    if (revents & POLLIN) {
        char buf[65536];
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);

        ssize_t n = recvfrom(s->fd, buf, sizeof(buf), 0, (struct sockaddr*)&peer_addr, &peer_len);
        if (n > 0) {
            if (IS_POINTER(s->on_message)) {
                Value buffer = create_buffer_from_string(buf, (size_t)n, "utf8");
                
                char ip_str[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &peer_addr.sin_addr, ip_str, sizeof(ip_str));
                
                Value rinfo = create_object();
                object_set(rinfo, create_string("address", 7), create_string(ip_str, strlen(ip_str)));
                object_set(rinfo, create_string("family", 6), create_string("IPv4", 4));
                object_set(rinfo, create_string("port", 4), make_integer(ntohs(peer_addr.sin_port)));
                object_set(rinfo, create_string("size", 4), make_integer(n));

                Value argv[2] = { buffer, rinfo };
                vm_call_function(s->vm, s->on_message, 2, argv);
            }
        }
    }
}

/* ── socket.on(event, cb) ───────────────────────────────────────────────── */

static Value js_dgram_on(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    DgramSocketHandle* s = (DgramSocketHandle*)env_to_ptr(this_val);
    if (!s || arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    JSString* ev = (JSString*)get_pointer(args[0]);
    if (strcmp(ev->data, "message") == 0)    s->on_message = args[1];
    else if (strcmp(ev->data, "error") == 0) s->on_error   = args[1];
    else if (strcmp(ev->data, "close") == 0) s->on_close   = args[1];
    
    return VAL_UNDEFINED;
}

/* ── socket.bind(port, address) ─────────────────────────────────────────── */

static Value js_dgram_bind(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    DgramSocketHandle* s = (DgramSocketHandle*)env_to_ptr(this_val);
    if (!s || s->fd < 0) return make_boolean(false);

    int port = 0;
    const char* host = "0.0.0.0";

    if (arg_count >= 1) {
        if (IS_INTEGER(args[0])) port = get_integer(args[0]);
        else if (IS_DOUBLE(args[0])) port = (int)get_double(args[0]);
    }

    if (arg_count >= 2 && IS_POINTER(args[1])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) host = ((JSString*)get_pointer(args[1]))->data;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (bind(s->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (IS_POINTER(s->on_error)) {
            Value err = create_system_error(vm, errno, "bind", host);
            Value argv[1] = { err };
            vm_call_function(s->vm, s->on_error, 1, argv);
        } else {
            vm_throw_error(vm, create_system_error(vm, errno, "bind", host));
        }
        return make_boolean(false);
    }

    return make_boolean(true);
}

/* ── socket.send(msg, offset, length, port, address) ────────────────────── */

static Value js_dgram_send(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    DgramSocketHandle* s = (DgramSocketHandle*)env_to_ptr(this_val);
    if (!s || s->fd < 0 || arg_count < 4) return make_boolean(false);

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

    int offset = 0;
    if (IS_INTEGER(args[1])) offset = get_integer(args[1]);
    else if (IS_DOUBLE(args[1])) offset = (int)get_double(args[1]);

    int msg_len = 0;
    if (IS_INTEGER(args[2])) msg_len = get_integer(args[2]);
    else if (IS_DOUBLE(args[2])) msg_len = (int)get_double(args[2]);

    if (offset < 0 || offset > (int)len || msg_len < 0 || offset + msg_len > (int)len) {
        return make_boolean(false);
    }

    int port = 0;
    if (IS_INTEGER(args[3])) port = get_integer(args[3]);
    else if (IS_DOUBLE(args[3])) port = (int)get_double(args[3]);

    const char* host = "127.0.0.1";
    if (arg_count >= 5 && IS_POINTER(args[4])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[4]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) host = ((JSString*)get_pointer(args[4]))->data;
    }

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &dest_addr.sin_addr);

    ssize_t sent = sendto(s->fd, data + offset, msg_len, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    
    return make_boolean(sent >= 0);
}

/* ── socket.close() ─────────────────────────────────────────────────────── */

static Value js_dgram_close(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    DgramSocketHandle* s = (DgramSocketHandle*)env_to_ptr(this_val);
    if (!s || s->fd < 0) return VAL_UNDEFINED;
    
    el_remove_io(g_event_loop, &s->io);
    close(s->fd); 
    s->fd = -1;

    if (IS_POINTER(s->on_close)) {
        vm_call_function(s->vm, s->on_close, 0, NULL);
    }

    return VAL_UNDEFINED;
}

/* ── socket.address() ───────────────────────────────────────────────────── */

static Value js_dgram_address(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    DgramSocketHandle* s = (DgramSocketHandle*)env_to_ptr(this_val);
    if (!s || s->fd < 0) return VAL_NULL;

    struct sockaddr_in actual;
    socklen_t alen = sizeof(actual);
    if (getsockname(s->fd, (struct sockaddr*)&actual, &alen) < 0) {
        return VAL_NULL;
    }

    char ip_str[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &actual.sin_addr, ip_str, sizeof(ip_str));

    Value addr = create_object();
    object_set(addr, create_string("address", 7), create_string(ip_str, strlen(ip_str)));
    object_set(addr, create_string("family", 6), create_string("IPv4", 4));
    object_set(addr, create_string("port", 4), make_integer(ntohs(actual.sin_port)));

    return addr;
}

/* ── _dgram.createSocket() ──────────────────────────────────────────────── */

static Value js_dgram_create_socket(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val; (void)arg_count; (void)args;
    ENFORCE_NET_ACCESS(vm);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "socket", "dgram.createSocket"));
        return VAL_UNDEFINED;
    }
    
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    make_nonblock(fd);

    DgramSocketHandle* s = (DgramSocketHandle*)calloc(1, sizeof(DgramSocketHandle));
    s->vm = vm;
    s->fd = fd;
    s->on_message = s->on_error = s->on_close = VAL_NULL;

    s->io.fd = fd;
    s->io.events = POLLIN;
    s->io.cb = dgram_io_cb;
    s->io.user_data = s;
    el_add_io(g_event_loop, &s->io);

    Value obj = create_object();
    object_set(obj, create_string("on",      2), make_bound_fn(js_dgram_on,      "on",      s));
    object_set(obj, create_string("bind",    4), make_bound_fn(js_dgram_bind,    "bind",    s));
    object_set(obj, create_string("send",    4), make_bound_fn(js_dgram_send,    "send",    s));
    object_set(obj, create_string("close",   5), make_bound_fn(js_dgram_close,   "close",   s));
    object_set(obj, create_string("address", 7), make_bound_fn(js_dgram_address, "address", s));

    return obj;
}

/* ── Module factory ─────────────────────────────────────────────────────── */

Value build_dgram_module(VM* vm) {
    (void)vm;
    Value exports = create_object();
    object_set(exports, create_string("createSocket", 12),
               create_native_function((void*)js_dgram_create_socket,
                                      create_string("createSocket", 12)));
    return exports;
}

void dgram_mark_gc_roots(GCTraceFn trace) {
    if (!g_event_loop) return;
    
    for (IOHandle* h = g_event_loop->io_handles; h; h = h->next) {
        if (h->cb == dgram_io_cb) {
            DgramSocketHandle* s = (DgramSocketHandle*)h->user_data;
            trace(&s->on_message);
            trace(&s->on_error);
            trace(&s->on_close);
        }
    }
}
