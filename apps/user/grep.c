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
    if (argc != 3) {
        printf("Usage: grep [PATTERN] [FILE]\n");
        return -1;
    }

    char *pattern = argv[1];
    char *filename = argv[2];

    int file_ino = dir_lookup(workdir_ino, filename);
    if (file_ino < 0) {
        printf("grep: file %s not found\n", filename);
        return -1;
    }

    char buf[BLOCK_SIZE + 1];
    char line[1024];
    int pos = 0;
    unsigned int block_no = 0;

    while (1) {
        file_read(file_ino, block_no, buf);
        block_no++;

        if (block_empty(buf))
            break;

        buf[BLOCK_SIZE] = '\0';

        for (int i = 0; i < BLOCK_SIZE; i++) {
            char c = buf[i];

            if (c == '\0')
                goto END;

            line[pos++] = c;

            if (c == '\n') {
                line[pos] = '\0';
                if (strstr(line, pattern))
                    printf("%s", line);
                pos = 0;
            }

            if (pos >= 1023) {
                line[pos] = '\0';
                if (strstr(line, pattern))
                    printf("%s\n", line);
                pos = 0;
            }
        }
    }

END:
    if (pos > 0) {
        line[pos] = '\0';
        if (strstr(line, pattern))
            printf("%s\n", line);
    }

    return 0;
}
