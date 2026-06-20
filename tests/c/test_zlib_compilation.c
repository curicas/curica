#include <cosmo.h>
#include "third_party/zlib/zlib.h"
#include <stdio.h>

int main() {
    printf("zlib version: %s\n", zlibVersion());
    return 0;
}
