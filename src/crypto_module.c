/**
 * @file crypto_module.c
 * @brief Node.js 'crypto' native bindings for Curica Runtime.
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
#include "crypto_module.h"
#include "alloc.h"
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <stdint.h>
#include <stdio.h>
#include "vm.h"
#include "napi.h"

// --- SHA256 Implementation ---
#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

typedef struct {
    uint32_t state[8];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA256_CTX;

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for ( ; i < 64; ++i)
        m[i] = SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + m[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85; ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c; ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count[0] = ctx->count[1] = 0;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
    size_t i = ctx->count[0] % 64;
    ctx->count[0] += len;
    if (ctx->count[0] < len) ctx->count[1]++;
    for (size_t j = 0; j < len; j++) {
        ctx->buffer[i++] = data[j];
        if (i == 64) {
            sha256_transform(ctx, ctx->buffer);
            i = 0;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i = ctx->count[0];
    uint8_t pad[64] = {0x80};
    uint8_t size[8];
    for (int j = 0; j < 8; j++) size[j] = (ctx->count[(7 - j) / 4] >> ((3 - (j % 4)) * 8)) & 255;
    size[4] = (i >> 21); size[5] = (i >> 13); size[6] = (i >> 5); size[7] = (i << 3);
    sha256_update(ctx, pad, i % 64 < 56 ? 56 - (i % 64) : 120 - (i % 64));
    sha256_update(ctx, size, 8);
    for (int j = 0; j < 32; j++) hash[j] = (ctx->state[j / 4] >> ((3 - (j % 4)) * 8)) & 255;
}

// --- JS Bindings ---

static Value js_crypto_randomUUID(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    char uuid[37];
    const char* chars = "0123456789abcdef";
    for(int i=0; i<36; i++) {
        if(i==8 || i==13 || i==18 || i==23) {
            uuid[i] = '-';
        } else if(i==14) {
            uuid[i] = '4';
        } else {
            uuid[i] = chars[rand() % 16];
        }
    }
    uuid[36] = '\0';
    return create_string(uuid, 36);
}

static Value js_crypto_randomBytes(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || (!IS_INTEGER(args[0]) && !IS_DOUBLE(args[0]))) return VAL_UNDEFINED;
    double size_d = IS_INTEGER(args[0]) ? get_integer(args[0]) : get_double(args[0]);
    if (size_d < 0) return VAL_UNDEFINED;
    uint32_t size = (uint32_t)size_d;

    Value buf_val = create_buffer(size, false);
    if (!IS_POINTER(buf_val)) return VAL_UNDEFINED;
    JSBuffer* buf = (JSBuffer*)get_pointer(buf_val);
    
    if (size > 0) {
        getrandom(buf->data, size, 0);
    }
    return buf_val;
}

static void hash_finalize(napi_env env, void* data, void* hint) {
    (void)env; (void)hint;
    free(data);
}

static Value js_hash_update(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    
    napi_env env = (napi_env)vm;
    napi_value js_obj = (napi_value)(uintptr_t)this_val;
    SHA256_CTX* ctx = NULL;
    napi_unwrap(env, js_obj, (void**)&ctx);
    if (!ctx) return VAL_UNDEFINED;

    if (arg_count >= 1 && IS_POINTER(args[0])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* str = (JSString*)get_pointer(args[0]);
            sha256_update(ctx, (uint8_t*)str->data, str->length);
        } else if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* b = (JSBuffer*)get_pointer(args[0]);
            sha256_update(ctx, b->data, b->length);
        }
    }
    return this_val; // Chainable
}

static Value js_hash_digest(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    
    napi_env env = (napi_env)vm;
    napi_value js_obj = (napi_value)(uintptr_t)this_val;
    SHA256_CTX* ctx = NULL;
    napi_unwrap(env, js_obj, (void**)&ctx);
    if (!ctx) return VAL_UNDEFINED;

    uint8_t hash[32];
    sha256_final(ctx, hash);

    bool hex = false;
    if (arg_count >= 1 && IS_POINTER(args[0])) {
        JSString* enc = (JSString*)get_pointer(args[0]);
        if (strcmp(enc->data, "hex") == 0) hex = true;
    }

    if (hex) {
        char hex_out[65];
        for (int i = 0; i < 32; i++) {
            sprintf(&hex_out[i * 2], "%02x", hash[i]);
        }
        hex_out[64] = '\0';
        return create_string(hex_out, 64);
    } else {
        Value buf_val = create_buffer(32, false);
        JSBuffer* b = (JSBuffer*)get_pointer(buf_val);
        memcpy(b->data, hash, 32);
        return buf_val;
    }
}

static Value js_crypto_createHash(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* alg = (JSString*)get_pointer(args[0]);
    if (strcmp(alg->data, "sha256") != 0) {
        // Only sha256 supported
        return VAL_UNDEFINED;
    }

    Value hash_obj = create_object();
    SHA256_CTX* ctx = (SHA256_CTX*)malloc(sizeof(SHA256_CTX));
    sha256_init(ctx);
    
    napi_env env = (napi_env)vm;
    napi_value js_obj = (napi_value)(uintptr_t)hash_obj;
    napi_wrap(env, js_obj, ctx, hash_finalize, NULL, NULL);

    object_set(hash_obj, create_string("update", 6), create_bound_native_function((void*)js_hash_update, create_string("update", 6), hash_obj));
    object_set(hash_obj, create_string("digest", 6), create_bound_native_function((void*)js_hash_digest, create_string("digest", 6), hash_obj));

    return hash_obj;
}

Value build_crypto_module(VM* vm) {
    (void)vm;
    Value crypto_obj = create_object();
    object_set(crypto_obj, create_string("randomUUID", 10), create_native_function((void*)js_crypto_randomUUID, create_string("randomUUID", 10)));
    object_set(crypto_obj, create_string("randomBytes", 11), create_native_function((void*)js_crypto_randomBytes, create_string("randomBytes", 11)));
    object_set(crypto_obj, create_string("createHash", 10), create_native_function((void*)js_crypto_createHash, create_string("createHash", 10)));
    return crypto_obj;
}

// --- Internal API for WebSockets ---

#define SHA1_ROTL(bits, word) (((word) << (bits)) | ((word) >> (32 - (bits))))

void crypto_sha1(const uint8_t* data, size_t len, uint8_t hash[20]) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    uint64_t bit_len = (uint64_t)len * 8;
    size_t new_len = len + 1;
    while (new_len % 64 != 56) new_len++;

    uint8_t* padded = (uint8_t*)calloc(new_len + 8, 1);
    memcpy(padded, data, len);
    padded[len] = 0x80;

    for (int i = 0; i < 8; i++) {
        padded[new_len + i] = (bit_len >> ((7 - i) * 8)) & 0xFF;
    }

    for (size_t offset = 0; offset < new_len + 8; offset += 64) {
        uint32_t w[80];
        const uint8_t* chunk = padded + offset;

        for (int i = 0; i < 16; i++) {
            w[i] = (chunk[i*4] << 24) | (chunk[i*4+1] << 16) | (chunk[i*4+2] << 8) | chunk[i*4+3];
        }
        for (int i = 16; i < 80; i++) {
            w[i] = SHA1_ROTL(1, w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = SHA1_ROTL(5, a) + f + e + k + w[i];
            e = d;
            d = c;
            c = SHA1_ROTL(30, b);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    free(padded);

    for (int i = 0; i < 4; i++) {
        hash[i]      = (h0 >> (24 - i * 8)) & 0xFF;
        hash[i + 4]  = (h1 >> (24 - i * 8)) & 0xFF;
        hash[i + 8]  = (h2 >> (24 - i * 8)) & 0xFF;
        hash[i + 12] = (h3 >> (24 - i * 8)) & 0xFF;
        hash[i + 16] = (h4 >> (24 - i * 8)) & 0xFF;
    }
}

char* crypto_base64_encode(const uint8_t* src, size_t len) {
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = 4 * ((len + 2) / 3);
    char* out = (char*)malloc(out_len + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? src[i++] : 0;
        uint32_t octet_b = i < len ? src[i++] : 0;
        uint32_t octet_c = i < len ? src[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }

    if (len % 3 == 1) {
        out[out_len - 1] = '=';
        out[out_len - 2] = '=';
    } else if (len % 3 == 2) {
        out[out_len - 1] = '=';
    }
    
    out[out_len] = '\0';
    return out;
}
