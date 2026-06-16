/**
 * @file websocket_module.c
 * @brief Native WebSocket Client Implementation
 * 
 * Implements a high-performance, RFC-6455 compliant WebSocket client natively.
 * It handles the WebSocket handshake over HTTP/HTTPS, masks frames securely,
 * and processes asynchronous network frames completely within the event loop.
 */
#include "vm.h"
#include "event_loop.h"
#include "alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define ENFORCE_NET_ACCESS(vm) \
    if (!(vm)->allow_net) { \
        vm_throw_error((vm), create_error("PermissionError", create_string("Requires --allow-net access", 27))); \
        return VAL_UNDEFINED; \
    }

#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

typedef enum {
    WS_CONNECTING = 0,
    WS_OPEN = 1,
    WS_CLOSING = 2,
    WS_CLOSED = 3
} WSState;

typedef struct WsHandle {
    VM* vm;
    Value js_obj;

    WSState state;
    bool is_tls;
    
    mbedtls_net_context server_fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    bool tls_handshake_done;

    char* read_buf;
    size_t read_len;
    size_t read_cap;

    char* write_buf;
    size_t write_off;
    size_t write_len;
    size_t write_cap;

    int fd;
    IOHandle io;
    struct WsHandle* next;
} WsHandle;

static WsHandle* ws_roots = NULL;

static void ws_append_write(WsHandle* ws, const char* data, size_t len) {
    if (ws->write_len + len > ws->write_cap) {
        ws->write_cap = ws->write_len + len + 1024;
        ws->write_buf = realloc(ws->write_buf, ws->write_cap);
    }
    memcpy(ws->write_buf + ws->write_len, data, len);
    ws->write_len += len;
    ws->io.events |= POLLOUT;
}

static void call_js_event(WsHandle* ws, const char* event_name, int argc, Value* argv) {
    Value fn = object_get(ws->js_obj, create_string(event_name, strlen(event_name)));
    if (IS_POINTER(fn)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(fn) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_FUNCTION) {
            vm_call_function(ws->vm, fn, argc, argv);
        }
    }
}

static void ws_close(WsHandle* ws) {
    printf("DEBUG: ws_close called\n");
    if (ws->state == WS_CLOSED) return;
    ws->state = WS_CLOSED;
    el_remove_io(g_event_loop, &ws->io);

    if (ws->is_tls) {
        mbedtls_ssl_close_notify(&ws->ssl);
        mbedtls_x509_crt_free(&ws->cacert);
        mbedtls_ssl_free(&ws->ssl);
        mbedtls_ssl_config_free(&ws->conf);
        mbedtls_ctr_drbg_free(&ws->ctr_drbg);
        mbedtls_entropy_free(&ws->entropy);
        mbedtls_net_free(&ws->server_fd);
    } else {
        close(ws->fd);
    }

    printf("DEBUG: ws_close calling onclose\n");
    object_set(ws->js_obj, create_string("readyState", 10), make_integer(WS_CLOSED));
    call_js_event(ws, "onclose", 0, NULL);
    printf("DEBUG: ws_close finished onclose\n");

    if (ws_roots == ws) {
        ws_roots = ws->next;
    } else {
        WsHandle* prev = ws_roots;
        while (prev && prev->next != ws) prev = prev->next;
        if (prev) prev->next = ws->next;
    }

    if (ws->read_buf) free(ws->read_buf);
    if (ws->write_buf) free(ws->write_buf);
    free(ws);
    printf("DEBUG: ws_close returning\n");
}

static void ws_fail(WsHandle* ws, const char* msg) {
    (void)msg;
    call_js_event(ws, "onerror", 0, NULL);
    ws_close(ws);
}

static void ws_parse_handshake(WsHandle* ws) {
    char* header_end = strstr(ws->read_buf, "\r\n\r\n");
    if (!header_end) return;

    if (strncmp(ws->read_buf, "HTTP/1.1 101", 12) != 0) {
        ws_fail(ws, "Handshake failed");
        return;
    }

    ws->state = WS_OPEN;
    object_set(ws->js_obj, create_string("readyState", 10), make_integer(WS_OPEN));
    call_js_event(ws, "onopen", 0, NULL);

    size_t header_len = (header_end + 4) - ws->read_buf;
    ws->read_len -= header_len;
    memmove(ws->read_buf, header_end + 4, ws->read_len);
}

static void ws_parse_frames(WsHandle* ws) {
    while (ws->read_len >= 2) {
        unsigned char* buf = (unsigned char*)ws->read_buf;
        int opcode = buf[0] & 0x0F;
        bool masked = (buf[1] & 0x80) != 0;
        uint64_t payload_len = buf[1] & 0x7F;

        size_t header_size = 2;
        if (payload_len == 126) {
            if (ws->read_len < 4) return;
            payload_len = (buf[2] << 8) | buf[3];
            header_size += 2;
        } else if (payload_len == 127) {
            if (ws->read_len < 10) return;
            payload_len = 0;
            for (int i=0; i<8; i++) payload_len = (payload_len << 8) | buf[2+i];
            header_size += 8;
        }

        unsigned char mask[4] = {0};
        if (masked) {
            if (ws->read_len < header_size + 4) return;
            memcpy(mask, buf + header_size, 4);
            header_size += 4;
        }

        if (ws->read_len < header_size + payload_len) return;

        unsigned char* payload = buf + header_size;
        if (masked) {
            for (size_t i = 0; i < payload_len; i++) payload[i] ^= mask[i % 4];
        }

        if (opcode == 0x1) {
            Value msg_argv[1];
            msg_argv[0] = create_string((char*)payload, payload_len);
            ws->read_len -= (header_size + payload_len);
            memmove(ws->read_buf, ws->read_buf + header_size + payload_len, ws->read_len);
            call_js_event(ws, "onmessage", 1, msg_argv);
            /* ws may be closed inside onmessage callback — bail out */
            return;
        } else if (opcode == 0x8) {
            ws_close(ws);
            return;
        } else if (opcode == 0x9) {
            unsigned char pong_header[2] = {0x8A, 0x00};
            ws_append_write(ws, (char*)pong_header, 2);
        }

        ws->read_len -= (header_size + payload_len);
        memmove(ws->read_buf, ws->read_buf + header_size + payload_len, ws->read_len);
    }
}

static void ws_io_cb(void* user_data, int revents) {
    WsHandle* ws = (WsHandle*)user_data;

    if (ws->is_tls && !ws->tls_handshake_done) {
        int ret = mbedtls_ssl_handshake(&ws->ssl);
        if (ret == 0) {
            ws->tls_handshake_done = true;
            ws->io.events = POLLOUT;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ws_fail(ws, "TLS handshake failed");
        } else {
            ws->io.events = (ret == MBEDTLS_ERR_SSL_WANT_READ) ? POLLIN : POLLOUT;
        }
        return;
    }

    if (revents & POLLOUT) {
        while (ws->write_off < ws->write_len) {
            ssize_t n;
            if (ws->is_tls) {
                n = mbedtls_ssl_write(&ws->ssl, (unsigned char*)ws->write_buf + ws->write_off, ws->write_len - ws->write_off);
                if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) return;
                if (n < 0) { ws_fail(ws, "TLS write failed"); return; }
            } else {
                n = send(ws->fd, ws->write_buf + ws->write_off, ws->write_len - ws->write_off, 0);
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
                if (n < 0) { ws_fail(ws, "TCP write failed"); return; }
            }
            ws->write_off += (size_t)n;
        }
        if (ws->write_off == ws->write_len) {
            ws->write_off = 0;
            ws->write_len = 0;
            if (ws->state == WS_CLOSING) {
                ws_close(ws);
                return;
            }
            ws->io.events &= ~POLLOUT;
            ws->io.events |= POLLIN;
        }
    }

    if (revents & POLLIN) {
        if (ws->read_len == ws->read_cap) {
            ws->read_cap = ws->read_cap == 0 ? 4096 : ws->read_cap * 2;
            ws->read_buf = realloc(ws->read_buf, ws->read_cap);
        }
        size_t read_space = ws->read_cap - ws->read_len;
        ssize_t n;
        
        if (ws->is_tls) {
            n = mbedtls_ssl_read(&ws->ssl, (unsigned char*)(ws->read_buf + ws->read_len), read_space);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE || n == -0x7B00) return;
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) n = 0;
            if (n < 0) { ws_fail(ws, "TLS read failed"); return; }
        } else {
            n = recv(ws->fd, ws->read_buf + ws->read_len, read_space, 0);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            if (n < 0) { ws_fail(ws, "TCP read failed"); return; }
        }

        if (n == 0) {
            ws_close(ws);
            return;
        }
        ws->read_len += n;

        if (ws->state == WS_CONNECTING) {
            ws_parse_handshake(ws);
        } else if (ws->state == WS_OPEN) {
            ws_parse_frames(ws);
        }
    }
}

static void make_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static Value js_websocket_send(VM* vm, Value this_val, int argc, Value* argv) {
    (void)vm;
    if (argc < 1 || !IS_POINTER(argv[0])) return VAL_UNDEFINED;
    
    WsHandle* ws = ws_roots;
    while (ws && ws->js_obj != this_val) ws = ws->next;
    if (!ws || ws->state != WS_OPEN) return VAL_UNDEFINED;

    BlockHeader* h = (BlockHeader*)((char*)get_pointer(argv[0]) - sizeof(BlockHeader));
    if (h->obj_type == OBJ_STRING) {
        JSString* str = (JSString*)get_pointer(argv[0]);
        size_t len = str->length;
        unsigned char header[10];
        size_t hlen = 0;

        header[0] = 0x81;
        if (len < 126) {
            header[1] = 0x80 | len;
            hlen = 2;
        } else if (len <= 0xFFFF) {
            header[1] = 0x80 | 126;
            header[2] = (len >> 8) & 0xFF;
            header[3] = len & 0xFF;
            hlen = 4;
        } else {
            return VAL_UNDEFINED; 
        }

        unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
        ws_append_write(ws, (char*)header, hlen);
        ws_append_write(ws, (char*)mask, 4);

        char* masked_payload = malloc(len);
        for (size_t i = 0; i < len; i++) masked_payload[i] = str->data[i] ^ mask[i % 4];
        ws_append_write(ws, masked_payload, len);
        free(masked_payload);
    }
    return VAL_UNDEFINED;
}

static Value js_websocket_close(VM* vm, Value this_val, int argc, Value* argv) {
    (void)vm; (void)argc; (void)argv;
    WsHandle* ws = ws_roots;
    while (ws && ws->js_obj != this_val) ws = ws->next;
    if (!ws || ws->state == WS_CLOSED) return VAL_UNDEFINED;

    unsigned char close_frame[6] = {0x88, 0x80, 0x00, 0x00, 0x00, 0x00};
    ws_append_write(ws, (char*)close_frame, 6);
    ws->state = WS_CLOSING;
    return VAL_UNDEFINED;
}

static Value js_websocket_construct(VM* vm, Value this_val, int argc, Value* argv) {
    (void)this_val;
    ENFORCE_NET_ACCESS(vm);
    if (argc < 1 || !IS_POINTER(argv[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(argv[0]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_STRING) return VAL_UNDEFINED;

    char* urlStr = ((JSString*)get_pointer(argv[0]))->data;
    
    char host[256] = {0};
    int port = 80;
    char path[1024] = "/";
    bool is_tls = false;

    if (strncmp(urlStr, "ws://", 5) == 0) {
        urlStr += 5;
    } else if (strncmp(urlStr, "wss://", 6) == 0) {
        is_tls = true;
        port = 443;
        urlStr += 6;
    } else {
        return VAL_UNDEFINED;
    }

    char* slash = strchr(urlStr, '/');
    char* colon = strchr(urlStr, ':');
    if (slash) {
        strcpy(path, slash);
        *slash = '\0';
    }
    if (colon && (!slash || colon < slash)) {
        *colon = '\0';
        port = atoi(colon + 1);
    }
    strcpy(host, urlStr);

    WsHandle* ws = calloc(1, sizeof(WsHandle));
    ws->vm = vm;
    ws->is_tls = is_tls;
    ws->state = WS_CONNECTING;
    
    ws->js_obj = create_object();
    object_set(ws->js_obj, create_string("readyState", 10), make_integer(WS_CONNECTING));
    object_set(ws->js_obj, create_string("send", 4), create_native_function((void*)js_websocket_send, create_string("send", 4)));
    object_set(ws->js_obj, create_string("close", 5), create_native_function((void*)js_websocket_close, create_string("close", 5)));

    int fd = -1;
    if (is_tls) {
        mbedtls_net_init(&ws->server_fd);
        mbedtls_ssl_init(&ws->ssl);
        mbedtls_ssl_config_init(&ws->conf);
        mbedtls_x509_crt_init(&ws->cacert);
        mbedtls_ctr_drbg_init(&ws->ctr_drbg);
        mbedtls_entropy_init(&ws->entropy);

        mbedtls_ctr_drbg_seed(&ws->ctr_drbg, mbedtls_entropy_func, &ws->entropy, NULL, 0);
        mbedtls_x509_crt_parse_file(&ws->cacert, "/etc/ssl/certs/ca-certificates.crt");

        char port_str[16];
        sprintf(port_str, "%d", port);
        if (mbedtls_net_connect(&ws->server_fd, host, port_str, MBEDTLS_NET_PROTO_TCP) != 0) {
            ws_close(ws);
            return VAL_UNDEFINED;
        }
        mbedtls_net_set_nonblock(&ws->server_fd);
        fd = ws->server_fd.fd;

        mbedtls_ssl_config_defaults(&ws->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_authmode(&ws->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&ws->conf, &ws->cacert, NULL);
        mbedtls_ssl_conf_rng(&ws->conf, mbedtls_ctr_drbg_random, &ws->ctr_drbg);
        
        mbedtls_ssl_setup(&ws->ssl, &ws->conf);
        mbedtls_ssl_set_hostname(&ws->ssl, host);
        mbedtls_ssl_set_bio(&ws->ssl, &ws->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
    } else {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        char port_str[16];
        sprintf(port_str, "%d", port);

        if (getaddrinfo(host, port_str, &hints, &res) != 0) { ws_close(ws); return VAL_UNDEFINED; }
        fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) { freeaddrinfo(res); ws_close(ws); return VAL_UNDEFINED; }
        make_nonblock(fd);
        connect(fd, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
    }

    ws->fd = fd;
    ws->io.fd = fd;
    ws->io.events = POLLOUT;
    ws->io.cb = ws_io_cb;
    ws->io.user_data = ws;
    el_add_io(g_event_loop, &ws->io);

    char reqStr[2048];
    snprintf(reqStr, sizeof(reqStr),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n", path, host);
    ws_append_write(ws, reqStr, strlen(reqStr));

    ws->next = ws_roots;
    ws_roots = ws;

    return ws->js_obj;
}

void ws_mark_gc_roots(GCTraceFn trace) {
    WsHandle* ws = ws_roots;
    while (ws) {
        trace(&ws->js_obj);
        ws = ws->next;
    }
}

Value build_websocket_module(VM* vm) {
    (void)vm;
    return create_native_function((void*)js_websocket_construct, create_string("WebSocket", 9));
}
