#include <stdio.h>
#include <dlfcn.h>
#include <cosmo.h>

int main() {
    void* handle = cosmo_dlopen("libffi.so.8", RTLD_LAZY);
    if (!handle) handle = cosmo_dlopen("libffi.so.7", RTLD_LAZY);
    if (!handle) handle = cosmo_dlopen("libffi.so", RTLD_LAZY);
    if (!handle) return 1;
    
    void* type_sint32 = cosmo_dlsym(handle, "ffi_type_sint32");
    void* type_double = cosmo_dlsym(handle, "ffi_type_double");
    void* type_pointer = cosmo_dlsym(handle, "ffi_type_pointer");
    
    printf("sint32: %p, double: %p, pointer: %p\n", type_sint32, type_double, type_pointer);
    return 0;
}
