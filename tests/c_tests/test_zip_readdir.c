#include <stdio.h>
#include <sys/stat.h>

int main() {
    struct stat st;
    if (stat("/zip/test_dir/foo/bar.txt", &st) == 0) {
        printf("found bar.txt, size %ld\n", st.st_size);
    } else {
        printf("stat failed\n");
    }
    return 0;
}
