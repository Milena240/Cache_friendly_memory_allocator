#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int main() {
    size_t huge_page_size = 2 * 1024 * 1024;
    size_t num_pages = 4;
    size_t length = num_pages * huge_page_size;

    void *addr = mmap(NULL,
                      length,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                      -1,
                      0);

    if (addr == MAP_FAILED) {
        perror("mmap failed");
        printf("Hint: did you configure huge pages?\n");
        return 1;
    }

    printf("Allocated %zu MB using huge pages at %p\n", length / (1024 * 1024), addr);

    memset(addr, 0, length);

    printf("Memory initialized successfully.\n");

    printf("PID: %d\n", getpid());
    printf("Press Enter to exit...\n");
    getchar();

    if (munmap(addr, length) != 0) {
        perror("munmap failed");
        return 1;
    }

    return 0;
}
