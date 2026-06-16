#ifndef VALUE_H
#define VALUE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief NaN-Boxed Value Representation
 * 
 * A `Value` is strictly represented as a 64-bit IEEE 754 NaN-boxed word.
 * - If the value is < 0xfff8000000000000ULL, it natively represents a double-precision float.
 * - If the value is >= 0xfff8000000000000ULL, the upper 16 bits encode the Type Tag (Pointer, Integer, Boolean, Null, Undefined, Symbol).
 * 
 * This enables instantaneous type checking via simple bitwise masks and completely eliminates
 * structural overhead for JS primitives. Pointers safely fit within the lower 48 bits, which 
 * aligns perfectly with x86_64 / ARM64 user-space virtual addressing.
 */
typedef uint64_t Value;

#define IS_DOUBLE(v) ((v) < 0xfff8000000000000ULL)
#define IS_TAGGED(v) ((v) >= 0xfff8000000000000ULL)

#define TAG_POINTER       0xfff8ULL
#define TAG_INTEGER       0xfff9ULL
#define TAG_BOOLEAN       0xfffaULL
#define TAG_NULL          0xfffbULL
#define TAG_UNDEFINED     0xfffcULL
#define TAG_SYMBOL        0xfffdULL  // Symbol primitive (ES6+)
#define TAG_MAGIC         0xfffeULL  // Internal VM sentinels

#define GET_TAG(v) ((v) >> 48)

#define IS_POINTER(v)   (IS_TAGGED(v) && GET_TAG(v) == TAG_POINTER)
#define IS_INTEGER(v)   (IS_TAGGED(v) && GET_TAG(v) == TAG_INTEGER)
#define IS_BOOLEAN(v)   (IS_TAGGED(v) && GET_TAG(v) == TAG_BOOLEAN)
#define IS_NULL(v)      (IS_TAGGED(v) && GET_TAG(v) == TAG_NULL)
#define IS_UNDEFINED(v) (IS_TAGGED(v) && GET_TAG(v) == TAG_UNDEFINED)
#define IS_SYMBOL(v)    (IS_TAGGED(v) && GET_TAG(v) == TAG_SYMBOL)
#define IS_MAGIC(v)     (IS_TAGGED(v) && GET_TAG(v) == TAG_MAGIC)

// Constructors and extractors for Doubles
static inline Value make_double(double d) {
    union {
        double d;
        uint64_t u;
    } cast;
    cast.d = d;
    // If the double is a NaN, normalize it to a positive quiet NaN (0x7ff8000000000000ULL)
    // to prevent it from overlapping with our tagged space.
    if ((cast.u & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL && (cast.u & 0x000fffffffffffffULL) != 0) {
        return 0x7ff8000000000000ULL;
    }
    return cast.u;
}

static inline double get_double(Value v) {
    union {
        uint64_t u;
        double d;
    } cast;
    cast.u = v;
    return cast.d;
}

// Constructors and extractors for Tagged types
static inline Value make_pointer(void* ptr) {
    return (TAG_POINTER << 48) | ((uintptr_t)ptr & 0x0000ffffffffffffULL);
}

static inline void* get_pointer(Value v) {
    return (void*)(uintptr_t)(v & 0x0000ffffffffffffULL);
}

static inline Value make_integer(int32_t val) {
    return (TAG_INTEGER << 48) | ((uint32_t)val);
}

static inline int32_t get_integer(Value v) {
    return (int32_t)(uint32_t)(v & 0xffffffffULL);
}

static inline Value make_boolean(bool val) {
    return (TAG_BOOLEAN << 48) | (val ? 1 : 0);
}

static inline bool get_boolean(Value v) {
    return (v & 1) != 0;
}

static inline Value make_symbol(uint32_t id) {
    return (TAG_SYMBOL << 48) | (uint64_t)id;
}

static inline uint32_t get_symbol_id(Value v) {
    return (uint32_t)(v & 0xffffffffULL);
}

#define VAL_NULL          ((Value)(TAG_NULL      << 48))
#define VAL_UNDEFINED     ((Value)(TAG_UNDEFINED << 48))
#define VAL_TRUE          ((Value)(TAG_BOOLEAN   << 48) | 1)
#define VAL_FALSE         ((Value)(TAG_BOOLEAN   << 48) | 0)

// Magic internal VM values
#define VAL_EMPTY         ((Value)(TAG_MAGIC << 48) | 0) // Empty array slot / property hole
#define VAL_ITER_END      ((Value)(TAG_MAGIC << 48) | 1) // End of iterator marker
#define VAL_UNINITIALIZED ((Value)(TAG_MAGIC << 48) | 2) // TDZ: let/const before declaration

static inline bool is_truthy(Value v) {
    if (IS_DOUBLE(v)) {
        // Exclude 0, -0, and NaN
        if ((v & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL && (v & 0x000fffffffffffffULL) != 0) {
            return false;
        }
        double d = get_double(v);
        return d != 0.0 && d != -0.0;
    }
    if (IS_INTEGER(v)) {
        return get_integer(v) != 0;
    }
    if (IS_BOOLEAN(v)) {
        return get_boolean(v);
    }
    if (IS_NULL(v) || IS_UNDEFINED(v)) {
        return false;
    }
    return true; // Objects, arrays, functions, and strings are truthy
}

#endif // VALUE_H
