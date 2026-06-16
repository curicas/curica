#include <sys/random.h>
#include <stdio.h>
int main() {
    char buf[16];
    getrandom(buf, 16, 0);
    printf("Works\n");
    return 0;
}
