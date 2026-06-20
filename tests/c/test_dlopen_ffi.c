#include <stdio.h>
#include <dlfcn.h>
#include <cosmo.h>

int main() {
    void* handle = cosmo_dlopen("libffi.so.8", RTLD_LAZY);
    if (!handle) handle = cosmo_dlopen("libffi.so.7", RTLD_LAZY);
    if (!handle) handle = cosmo_dlopen("libffi.so", RTLD_LAZY);
    if (!handle) {
        printf("Failed to load libffi\n");
        return 1;
    }
    printf("Successfully loaded libffi at %p\n", handle);
    
    void* prep = cosmo_dlsym(handle, "ffi_prep_cif");
    void* call = cosmo_dlsym(handle, "ffi_call");
    printf("ffi_prep_cif: %p, ffi_call: %p\n", prep, call);
    
    return 0;
}
