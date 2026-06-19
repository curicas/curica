/**
 * @file ffi_module.c
 * @brief Foreign Function Interface (FFI) Bindings for Curica
 * 
 * Exposes a 'Curica.FFI' module enabling dynamic loading of native libraries
 * and execution of C functions without N-API wrapper code.
 */
#include "vm.h"
#include "alloc.h"
#include <cosmo.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

extern void* cosmo_dlopen(const char* filename, int flags);
extern void* cosmo_dlsym(void* handle, const char* symbol);

// --- FFI Structural Definitions ---
typedef struct _ffi_type {
  size_t size;
  unsigned short alignment;
  unsigned short type;
  struct _ffi_type **elements;
} ffi_type;

typedef enum ffi_abi {
  FFI_FIRST_ABI = 1,
  FFI_UNIX64 = 2,
  FFI_DEFAULT_ABI = FFI_UNIX64
} ffi_abi;

typedef struct {
  ffi_abi abi;
  unsigned nargs;
  ffi_type **arg_types;
  ffi_type *rtype;
  unsigned bytes;
  unsigned flags;
} ffi_cif;

typedef int ffi_status;

typedef struct ffi_closure ffi_closure;

static void* (*ffi_closure_alloc)(size_t size, void **code);
static ffi_status (*ffi_prep_closure_loc)(ffi_closure *closure, ffi_cif *cif, void (*fun)(ffi_cif *cif, void *ret, void **args, void *user_data), void *user_data, void *codeloc);
static void (*ffi_closure_free)(void *writable);

#ifndef FFI_TYPE_STRUCT
#define FFI_TYPE_STRUCT 13
#endif

// --- Dynamic pointers to host libffi.so ---
static void* libffi_handle = NULL;
static ffi_status (*ffi_prep_cif)(ffi_cif *cif, ffi_abi abi, unsigned int nargs, ffi_type *rtype, ffi_type **atypes);
static void (*ffi_call)(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue);

static ffi_type* ffi_type_void_ptr;
static ffi_type* ffi_type_uint8_ptr;
static ffi_type* ffi_type_sint8_ptr;
static ffi_type* ffi_type_uint16_ptr;
static ffi_type* ffi_type_sint16_ptr;
static ffi_type* ffi_type_uint32_ptr;
static ffi_type* ffi_type_sint32_ptr;
static ffi_type* ffi_type_uint64_ptr;
static ffi_type* ffi_type_sint64_ptr;
static ffi_type* ffi_type_float_ptr;
static ffi_type* ffi_type_double_ptr;
static ffi_type* ffi_type_pointer_ptr;

static bool init_libffi() {
    if (libffi_handle) return true;
    libffi_handle = cosmo_dlopen("libffi.so.8", RTLD_LAZY);
    if (!libffi_handle) libffi_handle = cosmo_dlopen("libffi.so.7", RTLD_LAZY);
    if (!libffi_handle) libffi_handle = cosmo_dlopen("libffi.so.6", RTLD_LAZY);
    if (!libffi_handle) libffi_handle = cosmo_dlopen("libffi.so", RTLD_LAZY);
    if (!libffi_handle) return false;

    ffi_prep_cif = cosmo_dlsym(libffi_handle, "ffi_prep_cif");
    ffi_call = cosmo_dlsym(libffi_handle, "ffi_call");
    ffi_closure_alloc = cosmo_dlsym(libffi_handle, "ffi_closure_alloc");
    ffi_prep_closure_loc = cosmo_dlsym(libffi_handle, "ffi_prep_closure_loc");
    ffi_closure_free = cosmo_dlsym(libffi_handle, "ffi_closure_free");
    
    ffi_type_void_ptr = cosmo_dlsym(libffi_handle, "ffi_type_void");
    ffi_type_uint8_ptr = cosmo_dlsym(libffi_handle, "ffi_type_uint8");
    ffi_type_sint8_ptr = cosmo_dlsym(libffi_handle, "ffi_type_sint8");
    ffi_type_uint16_ptr = cosmo_dlsym(libffi_handle, "ffi_type_uint16");
    ffi_type_sint16_ptr = cosmo_dlsym(libffi_handle, "ffi_type_sint16");
    ffi_type_uint32_ptr = cosmo_dlsym(libffi_handle, "ffi_type_uint32");
    ffi_type_sint32_ptr = cosmo_dlsym(libffi_handle, "ffi_type_sint32");
    ffi_type_uint64_ptr = cosmo_dlsym(libffi_handle, "ffi_type_uint64");
    ffi_type_sint64_ptr = cosmo_dlsym(libffi_handle, "ffi_type_sint64");
    ffi_type_float_ptr = cosmo_dlsym(libffi_handle, "ffi_type_float");
    ffi_type_double_ptr = cosmo_dlsym(libffi_handle, "ffi_type_double");
    ffi_type_pointer_ptr = cosmo_dlsym(libffi_handle, "ffi_type_pointer");
    
    return ffi_prep_cif && ffi_call && ffi_type_void_ptr;
}

static ffi_type* get_ffi_type(Value type_val) {
    if (IS_POINTER(type_val)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(type_val) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* type_str = (JSString*)get_pointer(type_val);
            const char* type_name = type_str->data;
            if (strcmp(type_name, "void") == 0) return ffi_type_void_ptr;
            if (strcmp(type_name, "int8") == 0) return ffi_type_sint8_ptr;
            if (strcmp(type_name, "uint8") == 0) return ffi_type_uint8_ptr;
            if (strcmp(type_name, "int16") == 0) return ffi_type_sint16_ptr;
            if (strcmp(type_name, "uint16") == 0) return ffi_type_uint16_ptr;
            if (strcmp(type_name, "int") == 0 || strcmp(type_name, "int32") == 0) return ffi_type_sint32_ptr;
            if (strcmp(type_name, "uint32") == 0) return ffi_type_uint32_ptr;
            if (strcmp(type_name, "int64") == 0 || strcmp(type_name, "long") == 0) return ffi_type_sint64_ptr;
            if (strcmp(type_name, "uint64") == 0 || strcmp(type_name, "ulong") == 0) return ffi_type_uint64_ptr;
            if (strcmp(type_name, "float") == 0) return ffi_type_float_ptr;
            if (strcmp(type_name, "double") == 0) return ffi_type_double_ptr;
            if (strcmp(type_name, "pointer") == 0) return ffi_type_pointer_ptr;
        } else if (h->obj_type == OBJ_OBJECT) {
            Value native_ptr_val = object_get(type_val, create_string("_ffi_type", 9));
            if (IS_POINTER(native_ptr_val)) return (ffi_type*)get_pointer(native_ptr_val);
        }
    }
    return ffi_type_pointer_ptr; // fallback
}

// ffi_loadLibrary(path)
static Value ffi_loadLibrary(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!init_libffi()) {
        vm_throw_error(vm, create_error("Error", create_string("Failed to load libffi from host system", 38)));
        return VAL_NULL;
    }

    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_NULL;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    void* handle = cosmo_dlopen(path_str->data, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        vm_throw_error(vm, create_error("Error", create_string("Failed to load dynamic library", 30)));
        return VAL_NULL;
    }
    return make_pointer(handle);
}

// ffi_getSymbol(handle, name)
static Value ffi_getSymbol(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_NULL;
    void* handle = get_pointer(args[0]);
    JSString* name_str = (JSString*)get_pointer(args[1]);
    
    void* symbol = cosmo_dlsym(handle, name_str->data);
    if (!symbol) {
        vm_throw_error(vm, create_error("Error", create_string("Symbol not found", 16)));
        return VAL_NULL;
    }
    return make_pointer(symbol);
}

// ffi_callSymbol(symbol, returnType, [argTypes], [args])
static Value ffi_callSymbol(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 4 || !IS_POINTER(args[0]) || !IS_POINTER(args[2]) || !IS_POINTER(args[3])) return VAL_NULL;
    void* symbol = get_pointer(args[0]);
    ffi_type* rtype = get_ffi_type(args[1]);
    JSArray* arg_types_arr = (JSArray*)get_pointer(args[2]);
    JSArray* js_args = (JSArray*)get_pointer(args[3]);
    
    unsigned int nargs = arg_types_arr->length;
    
    ffi_type** atypes = malloc(sizeof(ffi_type*) * nargs);
    for (unsigned int i = 0; i < nargs; i++) {
        atypes[i] = get_ffi_type(arg_types_arr->elements[i]);
    }
    
    ffi_cif cif;
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, rtype, atypes) != 0) { // FFI_OK == 0
        free(atypes);
        vm_throw_error(vm, create_error("Error", create_string("Failed to prepare FFI CIF", 25)));
        return VAL_NULL;
    }
    
    void** avalues = malloc(sizeof(void*) * nargs);
    uint64_t* c_args = malloc(sizeof(uint64_t) * nargs); // Large enough to hold doubles/64-bit ints
    
    for (unsigned int i = 0; i < nargs && i < js_args->length; i++) {
        Value val = js_args->elements[i];
        if (atypes[i]->type == FFI_TYPE_STRUCT) {
            void* ptr = IS_POINTER(val) ? get_pointer(val) : NULL;
            avalues[i] = ptr;
        } else if (atypes[i] == ffi_type_double_ptr || atypes[i] == ffi_type_float_ptr) {
            double d = IS_INTEGER(val) ? (double)get_integer(val) : get_double(val);
            if (atypes[i] == ffi_type_float_ptr) {
                float f = (float)d;
                memcpy(&c_args[i], &f, sizeof(float));
            } else {
                memcpy(&c_args[i], &d, sizeof(double));
            }
            avalues[i] = &c_args[i];
        } else if (atypes[i] == ffi_type_pointer_ptr) {
            void* ptr = IS_POINTER(val) ? get_pointer(val) : NULL;
            memcpy(&c_args[i], &ptr, sizeof(void*));
            avalues[i] = &c_args[i];
        } else {
            // Integer types
            long int_val = IS_INTEGER(val) ? get_integer(val) : 0;
            memcpy(&c_args[i], &int_val, sizeof(long));
            avalues[i] = &c_args[i];
        }
    }
    
    void* rvalue = NULL;
    uint64_t small_rvalue = 0;
    if (rtype->type == FFI_TYPE_STRUCT) {
        rvalue = malloc(rtype->size);
    } else {
        rvalue = &small_rvalue;
    }
    
    ffi_call(&cif, (void(*)(void))symbol, rvalue, avalues);
    
    free(atypes);
    free(avalues);
    free(c_args);
    
    if (rtype->type == FFI_TYPE_STRUCT) return make_pointer(rvalue);
    
    if (rtype == ffi_type_void_ptr) return VAL_UNDEFINED;
    if (rtype == ffi_type_pointer_ptr) return make_pointer((void*)small_rvalue);
    if (rtype == ffi_type_double_ptr) {
        double d;
        memcpy(&d, &small_rvalue, sizeof(double));
        return make_double(d);
    }
    if (rtype == ffi_type_float_ptr) {
        float f;
        memcpy(&f, &small_rvalue, sizeof(float));
        return make_double((double)f);
    }
    return make_integer((int32_t)small_rvalue);
}

static Value ffi_createStructType(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val; (void)vm;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_NULL;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_NULL;
    
    JSArray* arr = (JSArray*)get_pointer(args[0]);
    unsigned int nargs = arr->length;
    
    ffi_type* stype = malloc(sizeof(ffi_type));
    stype->size = 0;
    stype->alignment = 0;
    stype->type = FFI_TYPE_STRUCT;
    stype->elements = malloc(sizeof(ffi_type*) * (nargs + 1));
    
    for (unsigned int i = 0; i < nargs; i++) {
        stype->elements[i] = get_ffi_type(arr->elements[i]);
    }
    stype->elements[nargs] = NULL;
    
    Value obj = create_object();
    object_set(obj, create_string("_ffi_type", 9), make_pointer(stype));
    return obj;
}

typedef struct {
    VM* vm;
    Value js_func;
    ffi_type* rtype;
    unsigned int nargs;
    ffi_type** atypes;
} ffi_callback_data;

static void ffi_callback_dispatcher(ffi_cif *cif, void *ret, void **args, void *user_data) {
    (void)cif;
    ffi_callback_data* data = (ffi_callback_data*)user_data;
    VM* vm = data->vm;
    
    Value* js_args = malloc(sizeof(Value) * data->nargs);
    for (unsigned int i = 0; i < data->nargs; i++) {
        ffi_type* t = data->atypes[i];
        void* arg_ptr = args[i];
        if (t->type == FFI_TYPE_STRUCT) {
            js_args[i] = make_pointer(arg_ptr);
        } else if (t == ffi_type_double_ptr) {
            js_args[i] = make_double(*(double*)arg_ptr);
        } else if (t == ffi_type_float_ptr) {
            js_args[i] = make_double(*(float*)arg_ptr);
        } else if (t == ffi_type_pointer_ptr) {
            js_args[i] = make_pointer(*(void**)arg_ptr);
        } else {
            if (t->size == 8) {
                js_args[i] = make_integer(*(int64_t*)arg_ptr);
            } else {
                js_args[i] = make_integer(*(int32_t*)arg_ptr);
            }
        }
    }
    
    Value js_ret = vm_call_function(vm, data->js_func, data->nargs, js_args);
    free(js_args);
    
    if (data->rtype->type == FFI_TYPE_STRUCT) {
        void* struct_ptr = IS_POINTER(js_ret) ? get_pointer(js_ret) : NULL;
        if (struct_ptr) memcpy(ret, struct_ptr, data->rtype->size);
    } else if (data->rtype == ffi_type_double_ptr) {
        *(double*)ret = IS_INTEGER(js_ret) ? (double)get_integer(js_ret) : get_double(js_ret);
    } else if (data->rtype == ffi_type_float_ptr) {
        *(float*)ret = IS_INTEGER(js_ret) ? (float)get_integer(js_ret) : (float)get_double(js_ret);
    } else if (data->rtype == ffi_type_pointer_ptr) {
        *(void**)ret = IS_POINTER(js_ret) ? get_pointer(js_ret) : NULL;
    } else if (data->rtype != ffi_type_void_ptr) {
        if (data->rtype->size == 8) {
            *(int64_t*)ret = IS_INTEGER(js_ret) ? get_integer(js_ret) : (int64_t)get_double(js_ret);
        } else {
            *(int32_t*)ret = IS_INTEGER(js_ret) ? get_integer(js_ret) : (int32_t)get_double(js_ret);
        }
    }
}

static Value ffi_createCallback(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (!ffi_closure_alloc) return VAL_NULL;
    if (arg_count < 3 || !IS_POINTER(args[0]) || !IS_POINTER(args[2])) return VAL_NULL;
    
    Value js_func = args[0];
    ffi_type* rtype = get_ffi_type(args[1]);
    
    BlockHeader* h_args = (BlockHeader*)((char*)get_pointer(args[2]) - sizeof(BlockHeader));
    if (h_args->obj_type != OBJ_ARRAY) return VAL_NULL;
    JSArray* arr = (JSArray*)get_pointer(args[2]);
    unsigned int nargs = arr->length;
    
    ffi_type** atypes = malloc(sizeof(ffi_type*) * nargs);
    for (unsigned int i = 0; i < nargs; i++) {
        atypes[i] = get_ffi_type(arr->elements[i]);
    }
    
    ffi_cif* cif = malloc(sizeof(ffi_cif));
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, rtype, atypes) != 0) {
        free(atypes); free(cif); return VAL_NULL;
    }
    
    void* code = NULL;
    ffi_closure* closure = ffi_closure_alloc(256, &code);
    if (!closure) {
        free(atypes); free(cif); return VAL_NULL;
    }
    
    ffi_callback_data* data = malloc(sizeof(ffi_callback_data));
    data->vm = vm;
    data->js_func = js_func;
    data->rtype = rtype;
    data->nargs = nargs;
    data->atypes = atypes;
    
    vm_push_root(vm, js_func);
    
    if (ffi_prep_closure_loc(closure, cif, ffi_callback_dispatcher, data, code) != 0) {
        ffi_closure_free(closure); free(data); free(atypes); free(cif); return VAL_NULL;
    }
    
    return make_pointer(code);
}

void vm_register_ffi_module(VM* vm) {
    Value ffi_obj = create_object();
    
    object_set(ffi_obj, create_string("loadLibrary", 11), create_native_function((void*)ffi_loadLibrary, create_string("loadLibrary", 11)));
    object_set(ffi_obj, create_string("getSymbol", 9), create_native_function((void*)ffi_getSymbol, create_string("getSymbol", 9)));
    object_set(ffi_obj, create_string("callSymbol", 10), create_native_function((void*)ffi_callSymbol, create_string("callSymbol", 10)));
    object_set(ffi_obj, create_string("createStructType", 16), create_native_function((void*)ffi_createStructType, create_string("createStructType", 16)));
    object_set(ffi_obj, create_string("createCallback", 14), create_native_function((void*)ffi_createCallback, create_string("createCallback", 14)));
    
    // Mount it on the global object for easy access during development
    Value curica_obj = object_get(vm->global_obj, create_string("Curica", 6));
    if (!IS_POINTER(curica_obj)) {
        curica_obj = create_object();
        object_set(vm->global_obj, create_string("Curica", 6), curica_obj);
    }
    
    object_set(curica_obj, create_string("FFI", 3), ffi_obj);
}
