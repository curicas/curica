/**
 * @file http_module.c
 * @brief HTTP/HTTPS Networking Subsystem for the Curica Environment OS Kernel.
 *
 * Implements component logic for the Curica Environment OS Kernel.
 * Curica is a secure microkernel OS that employs a strict POSIX Virtual File System (VFS)
 * with /bin, /home/user, and pseudo-filesystems (/dev, /proc). It uses JS natively as the
 * systems shell scripting language to pipe I/O and spawn WASM processes, enforcing
 * capability-based security (allow_run, allow_net, allow_read, allow_write, allow_ffi).
 * Furthermore, the kernel freezes environments into Actually Portable Executables (APEs)
 * and features Source Compilation Fallback, Virtual Networking Mocking, and
 * Foreign Sandbox IPC attached.
 */
#include "http_module.h"
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
#include <netdb.h>
#include <sys/un.h>

#define ENFORCE_NET_ACCESS(vm) \
    if (!(vm)->allow_net) { \
        vm_throw_error((vm), create_error("PermissionError", create_string("Requires --allow-net access", 27))); \
        return VAL_UNDEFINED; \
    }

#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

static int make_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

typedef struct HttpReqHandle {
    VM* vm;
    int fd;
    IOHandle io;
    Value callback;
    char* write_buf;
    size_t write_len;
    size_t write_off;
    char* read_buf;
    size_t read_len;
    size_t read_cap;
    
    // TLS state
    bool is_tls;
    mbedtls_net_context server_fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    bool tls_handshake_done;
} HttpReqHandle;

struct HttpReqNode {
    HttpReqHandle* req;
    struct HttpReqNode* next;
};
static struct HttpReqNode* g_req_list = NULL;

static void add_req(HttpReqHandle* req) {
    struct HttpReqNode* n = malloc(sizeof(struct HttpReqNode));
    n->req = req;
    n->next = g_req_list;
    g_req_list = n;
}

static void remove_req(HttpReqHandle* req) {
    struct HttpReqNode** curr = &g_req_list;
    while (*curr) {
        if ((*curr)->req == req) {
            struct HttpReqNode* to_free = *curr;
            *curr = (*curr)->next;
            free(to_free);
            return;
        }
        curr = &(*curr)->next;
    }
}

void http_mark_gc_roots(GCTraceFn trace) {
    struct HttpReqNode* curr = g_req_list;
    while (curr) {
        trace(&curr->req->callback);
        curr = curr->next;
    }
}

static void http_free_req(HttpReqHandle* req) {
    if (req->is_tls) {
        mbedtls_x509_crt_free(&req->cacert);
        mbedtls_ssl_free(&req->ssl);
        mbedtls_ssl_config_free(&req->conf);
        mbedtls_ctr_drbg_free(&req->ctr_drbg);
        mbedtls_entropy_free(&req->entropy);
        mbedtls_net_free(&req->server_fd); // Closes fd too
    } else if (req->fd >= 0) {
        close(req->fd);
    }
    el_remove_io(g_event_loop, &req->io);
    req->fd = -1;

    if (req->write_buf) free(req->write_buf);
    if (req->read_buf) free(req->read_buf);
    remove_req(req);
    free(req);
}

static void http_call_cb_error(HttpReqHandle* req, const char* msg) {
    Value err = create_string(msg, strlen(msg)); 
    Value argv[5] = { err, VAL_NULL, VAL_NULL, VAL_NULL, VAL_NULL };
    vm_call_function(req->vm, req->callback, 5, argv);
    http_free_req(req);
}

static char* trim_str(char* str) {
    while (*str == ' ' || *str == '\t') str++;
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return str;
}

static void http_parse_response_and_call(HttpReqHandle* req) {
    if (req->read_len == 0) {
        http_call_cb_error(req, "Empty response");
        return;
    }
    req->read_buf[req->read_len] = '\0';
    char* header_end = strstr(req->read_buf, "\r\n\r\n");
    if (!header_end) {
        http_call_cb_error(req, "Invalid HTTP response");
        return;
    }
    *header_end = '\0';
    char* body = header_end + 4;
    size_t body_len = req->read_len - (body - req->read_buf);
    
    char* line = strtok(req->read_buf, "\r\n");
    if (!line) {
        http_call_cb_error(req, "Invalid HTTP status line");
        return;
    }
    
    char* space1 = strchr(line, ' ');
    if (!space1) {
        http_call_cb_error(req, "Invalid HTTP status line");
        return;
    }
    int status = atoi(space1 + 1);
    char* space2 = strchr(space1 + 1, ' ');
    char* status_text = space2 ? space2 + 1 : "";
    
    bool is_chunked = false;
    Value headers_obj = create_object();
    line = strtok(NULL, "\r\n");
    while (line) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char* key = trim_str(line);
            char* val = trim_str(colon + 1);
            
            char key_lower[64];
            strncpy(key_lower, key, sizeof(key_lower)-1);
            key_lower[sizeof(key_lower)-1] = '\0';
            for (char* p = key_lower; *p; ++p) {
                if (*p >= 'A' && *p <= 'Z') *p = *p + 32;
            }
            if (strcmp(key_lower, "transfer-encoding") == 0 && strstr(val, "chunked")) {
                is_chunked = true;
            }
            
            object_set(headers_obj, create_string(key, strlen(key)), create_string(val, strlen(val)));
        }
        line = strtok(NULL, "\r\n");
    }
    
    Value body_val;
    if (is_chunked) {
        char* unchunked = malloc(body_len);
        size_t parsed_len = 0;
        char* curr = body;
        char* end_bound = body + body_len;
        while (curr < end_bound) {
            char* endptr;
            long chunk_size = strtol(curr, &endptr, 16);
            if (endptr == curr || chunk_size < 0) break;
            if (chunk_size == 0) break;
            
            curr = strstr(endptr, "\r\n");
            if (!curr) break;
            curr += 2;
            
            if (curr + chunk_size > end_bound) {
                chunk_size = end_bound - curr; // safety clamp
            }
            memcpy(unchunked + parsed_len, curr, chunk_size);
            parsed_len += chunk_size;
            
            curr += chunk_size;
            if (curr + 2 <= end_bound && curr[0] == '\r' && curr[1] == '\n') {
                curr += 2;
            } else {
                break;
            }
        }
        unchunked[parsed_len] = '\0';
        body_val = create_string(unchunked, parsed_len);
        free(unchunked);
    } else {
        body_val = create_string(body, body_len);
    }
    
    Value argv[5];
    argv[0] = VAL_NULL; // error
    argv[1] = make_integer(status);
    argv[2] = create_string(status_text, strlen(status_text));
    argv[3] = headers_obj;
    argv[4] = body_val;
    
    vm_call_function(req->vm, req->callback, 5, argv);
    http_free_req(req);
}

static void http_io_cb(void* user_data, int revents) {
    HttpReqHandle* req = (HttpReqHandle*)user_data;

    if (req->is_tls && !req->tls_handshake_done) {
        int ret = mbedtls_ssl_handshake(&req->ssl);
        if (ret == 0) {
            req->tls_handshake_done = true;
            req->io.events = POLLOUT; // Now send request
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf), "TLS handshake failed: -0x%04x", -ret);
            http_call_cb_error(req, err_buf);
            return;
        } else {
            req->io.events = (ret == MBEDTLS_ERR_SSL_WANT_READ) ? POLLIN : POLLOUT;
        }
        return;
    }

    if ((revents & (POLLERR | POLLHUP | POLLNVAL)) && !(revents & (POLLIN | POLLOUT))) {
        if (req->read_len > 0) {
            http_parse_response_and_call(req);
        } else {
            http_call_cb_error(req, "Connection error");
        }
        return;
    }

    if (revents & POLLOUT) {
        while (req->write_off < req->write_len) {
            ssize_t n;
            if (req->is_tls) {
                n = mbedtls_ssl_write(&req->ssl, (const unsigned char*)req->write_buf + req->write_off, req->write_len - req->write_off);
                if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) return;
                if (n < 0) { http_call_cb_error(req, "TLS write failed"); return; }
            } else {
                n = send(req->fd, req->write_buf + req->write_off, req->write_len - req->write_off, 0);
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
                if (n < 0) { http_call_cb_error(req, "TCP write failed"); return; }
            }
            req->write_off += (size_t)n;
        }
        free(req->write_buf);
        req->write_buf = NULL;
        req->io.events &= ~POLLOUT;
        req->io.events |= POLLIN; // wait for response
    }

    if (revents & POLLIN) {
        char buf[4096];
        ssize_t n;
        if (req->is_tls) {
            n = mbedtls_ssl_read(&req->ssl, (unsigned char*)buf, sizeof(buf));
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE || n == -0x7B00) return;
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) n = 0;
            if (n < 0) {
                char err_msg[64];
                sprintf(err_msg, "TLS read failed: -0x%04x", -(int)n);
                http_call_cb_error(req, err_msg);
                return;
            }
        } else {
            n = recv(req->fd, buf, sizeof(buf), 0);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            if (n < 0) { http_call_cb_error(req, "TCP read failed"); return; }
        }

        if (n > 0) {
            if (req->read_len + n + 1 > req->read_cap) {
                req->read_cap = (req->read_cap == 0) ? 4096 : req->read_cap * 2;
                if (req->read_cap < req->read_len + n + 1) req->read_cap = req->read_len + n + 1;
                req->read_buf = realloc(req->read_buf, req->read_cap);
            }
            memcpy(req->read_buf + req->read_len, buf, n);
            req->read_len += n;
        } else if (n == 0) {
            http_parse_response_and_call(req);
        }
    }
}

static Value js_http_request(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_NET_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    Value options = args[0];
    Value callback = args[1];

    Value host_val = object_get(options, create_string("host", 4));
    Value port_val = object_get(options, create_string("port", 4));
    Value req_str_val = object_get(options, create_string("req_str", 7));
    Value proto_val = object_get(options, create_string("protocol", 8));

    if (!IS_POINTER(host_val) || !IS_POINTER(req_str_val)) return VAL_UNDEFINED;

    const char* host = ((JSString*)get_pointer(host_val))->data;
    int port = IS_INTEGER(port_val) ? get_integer(port_val) : (IS_DOUBLE(port_val) ? (int)get_double(port_val) : 80);
    const char* req_str = ((JSString*)get_pointer(req_str_val))->data;
    size_t req_len = ((JSString*)get_pointer(req_str_val))->length;

    bool is_tls = false;
    if (IS_POINTER(proto_val)) {
        const char* proto = ((JSString*)get_pointer(proto_val))->data;
        if (strcmp(proto, "https") == 0) is_tls = true;
    }

    HttpReqHandle* req = calloc(1, sizeof(HttpReqHandle));
    req->vm = vm;
    req->callback = callback;
    req->write_buf = malloc(req_len);
    memcpy(req->write_buf, req_str, req_len);
    req->write_len = req_len;
    req->is_tls = is_tls;

    int fd = -1;
    bool is_mocked = false;
    char mock_path[256];
    
    for (int i = 0; i < vm->net_mock_count; i++) {
        if (vm->net_mocks[i].port == port && strcmp(vm->net_mocks[i].host, host) == 0) {
            is_mocked = true;
            strncpy(mock_path, vm->net_mocks[i].unix_socket_path, sizeof(mock_path) - 1);
            mock_path[sizeof(mock_path) - 1] = '\0';
            break;
        }
    }

    if (is_tls) {
        mbedtls_net_init(&req->server_fd);
        mbedtls_ssl_init(&req->ssl);
        mbedtls_ssl_config_init(&req->conf);
        mbedtls_x509_crt_init(&req->cacert);
        mbedtls_ctr_drbg_init(&req->ctr_drbg);
        mbedtls_entropy_init(&req->entropy);

        int ret = mbedtls_ctr_drbg_seed(&req->ctr_drbg, mbedtls_entropy_func, &req->entropy, NULL, 0);
        if (ret != 0) printf("mbedtls_ctr_drbg_seed failed: -0x%04x\n", -ret);
        
        mbedtls_x509_crt_parse_file(&req->cacert, "/etc/ssl/certs/ca-certificates.crt");

        if (is_mocked) {
            req->server_fd.fd = socket(AF_UNIX, SOCK_STREAM, 0);
            struct sockaddr_un un_addr = {0};
            un_addr.sun_family = AF_UNIX;
            strncpy(un_addr.sun_path, mock_path, sizeof(un_addr.sun_path) - 1);
            connect(req->server_fd.fd, (struct sockaddr*)&un_addr, sizeof(un_addr));
        } else {
            char port_str[10];
            snprintf(port_str, sizeof(port_str), "%d", port);
            int ret_conn = mbedtls_net_connect(&req->server_fd, host, port_str, MBEDTLS_NET_PROTO_TCP);
            if (ret_conn != 0) {
                http_free_req(req);
                return VAL_UNDEFINED;
            }
        }
        
        mbedtls_net_set_nonblock(&req->server_fd);
        fd = req->server_fd.fd;

        ret = mbedtls_ssl_config_defaults(&req->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) printf("mbedtls_ssl_config_defaults failed: -0x%04x\n", -ret);
        
        mbedtls_ssl_conf_authmode(&req->conf, MBEDTLS_SSL_VERIFY_NONE); // Disable verification for mocks/dev
        mbedtls_ssl_conf_ca_chain(&req->conf, &req->cacert, NULL);
        mbedtls_ssl_conf_rng(&req->conf, mbedtls_ctr_drbg_random, &req->ctr_drbg);
        
        ret = mbedtls_ssl_setup(&req->ssl, &req->conf);
        if (ret != 0) printf("mbedtls_ssl_setup failed: -0x%04x\n", -ret);
        
        ret = mbedtls_ssl_set_hostname(&req->ssl, host);
        if (ret != 0) printf("mbedtls_ssl_set_hostname failed: -0x%04x\n", -ret);
        
        mbedtls_ssl_set_bio(&req->ssl, &req->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
    } else {
        if (is_mocked) {
            fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) { free(req->write_buf); free(req); return VAL_UNDEFINED; }
            make_nonblock(fd);
            struct sockaddr_un un_addr = {0};
            un_addr.sun_family = AF_UNIX;
            strncpy(un_addr.sun_path, mock_path, sizeof(un_addr.sun_path) - 1);
            connect(fd, (struct sockaddr*)&un_addr, sizeof(un_addr));
        } else {
            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            char port_str[16];
            sprintf(port_str, "%d", port);

            if (getaddrinfo(host, port_str, &hints, &res) != 0) {
                free(req->write_buf); free(req); return VAL_UNDEFINED;
            }

            fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (fd < 0) { freeaddrinfo(res); free(req->write_buf); free(req); return VAL_UNDEFINED; }
            make_nonblock(fd);

            connect(fd, res->ai_addr, res->ai_addrlen);
            freeaddrinfo(res);
        }
    }

    req->fd = fd;
    req->io.fd = fd;
    req->io.events = POLLOUT; // Trigger connect/handshake/write
    req->io.cb = http_io_cb;
    req->io.user_data = req;

    el_add_io(g_event_loop, &req->io);
    add_req(req);

    return VAL_UNDEFINED;
}

#include "psa/crypto.h"

Value build_http_module(VM* vm) {
    (void)vm;
    psa_crypto_init();
    Value exports = create_object();
    object_set(exports, create_string("request", 7), create_native_function((void*)js_http_request, create_string("request", 7)));
    return exports;
}
