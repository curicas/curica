#include <cosmo.h>
#include <stdio.h>
int main() {
    void* handle = cosmo_dlopen("./libtest.so", 0);
    if (!handle) {
        printf("dlopen error\n");
    } else {
        printf("dlopen success!\n");
    }
    return 0;
}
