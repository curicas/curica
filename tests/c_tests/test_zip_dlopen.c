#include <stdio.h>
#include <libc/dlopen/dlfcn.h>
int main() {
    void* handle = cosmo_dlopen("/zip/test_zip/test.so", RTLD_LAZY);
    if (!handle) {
        printf("failed: %s\n", cosmo_dlerror());
    } else {
        printf("success\n");
    }
    return 0;
}
