#include "app.h"
#include <string.h>

#ifndef BLOCK_SIZE
#define BLOCK_SIZE 512
#endif

static int block_empty(char *buf) {
    for (int i = 0; i < BLOCK_SIZE; i++)
        if (buf[i] != 0)
            return 0;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: wcl [FILE1] [FILE2] ...\n");
        return -1;
    }

    int total = 0;

    for (int a = 1; a < argc; a++) {

        char *filename = argv[a];
        int ino = dir_lookup(workdir_ino, filename);

        if (ino < 0) {
            printf("wcl: file %s not found\n", filename);
            return -1;
        }

        char buf[BLOCK_SIZE + 1];
        char line[1024];
        int pos = 0;
        unsigned int block_no = 0;

        while (1) {
            file_read(ino, block_no, buf);
            block_no++;

            if (block_empty(buf))
                break;

            buf[BLOCK_SIZE] = '\0';

            for (int i = 0; i < BLOCK_SIZE; i++) {
                char c = buf[i];

                if (c == '\0')
                    goto DONE;

                line[pos++] = c;

                if (c == '\n') {
                    total++;
                    pos = 0;
                }

                if (pos >= 1023) {
                    total++;
                    pos = 0;
                }
            }
        }

DONE:
        if (pos > 0)
            total++;
    }

    printf("%d\n", total);
    return 0;
}
