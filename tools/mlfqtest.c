#include "user.h"

int main() {
    int pid = getpid();

    printf("mlfqtest started (pid=%d)\n", pid);

    while (1) {
        printf("[PID %d] running...\n", pid);
        sleep(100000);   // 100 ms
    }

    return 0;
}
