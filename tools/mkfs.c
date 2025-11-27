/*
 * (C) 2025, Cornell University — modified for testfile support
 * Fully corrected mkfs generator for EGOS-2000
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "inode.h"

/* Kernel + system binaries */
char* egos_binaries[] = {
    "./egos.bin",
    "../build/release/sys_proc.elf",
    "../build/release/sys_terminal.elf",
    "../build/release/sys_file.elf",
    "../build/release/sys_shell.elf",
    "./images/Bohr.bmp"
};
#define EGOS_BIN_NUM ((sizeof(egos_binaries) / sizeof(char*)))

/* Initial FS layout */
char bin_dir[512] = "./   6 ../   0 ";

char* contents[] = {
    "./   0 ../   0 home/   1 bin/   6 ",                      /* inode 0 */
    "./   1 ../   0 yunhao/   2 rvr/   3 yacqub/   4 ",         /* inode 1 */
    "./   2 ../   1 README   5 ",                               /* inode 2 (/home/yunhao) — will be replaced */
    "./   3 ../   1 ",                                          /* inode 3 */
    "./   4 ../   1 ",                                          /* inode 4 */
    "With only 2000 lines of code, egos-2000 implements boot loader, SD card "
    "driver, tty driver, virtual memory with page tables, interrupt and "
    "exception handling, preemptive scheduler, system call, file system, "
    "shell, an Ethernet/UDP demo, several user commands, and the mkfs tool. "
    "Moreover, the EGOS book (https://egos.fun) contains 9 course projects.", /* inode 5 */
    bin_dir                                                       /* inode 6 (/bin) */
};
#define BIN_DIR_INODE ((sizeof(contents) / sizeof(char*)) - 1) /* = 6 */

char inode_buf[SIZE_2MB];
char tmp[512];
char vexriscv[SIZE_2MB * 2], exec[SIZE_2MB], fs[SIZE_2MB];

/* Load file into memory buffer */
int load_file(char* file_name, char* dst) {
    struct stat st;
    if (stat(file_name, &st) < 0) {
        printf("[ERROR] File not found: %s\n", file_name);
        return -1;
    }
    int fd = open(file_name, O_RDONLY);
    int total = 0;
    while (total < st.st_size) {
        int r = read(fd, dst + total, st.st_size - total);
        if (r <= 0) break;
        total += r;
    }
    close(fd);
    return total;
}

int getsize(inode_intf bs, uint ino) { return FILE_SYS_DISK_SIZE / BLOCK_SIZE; }
int setsize(inode_intf bs, uint ino, uint newsize) { assert(0); return 0; }

int ramread(inode_intf bs, uint ino, uint offset, block_t* block) {
    memcpy(block, fs + offset * BLOCK_SIZE, BLOCK_SIZE);
    return 0;
}

int ramwrite(inode_intf bs, uint ino, uint offset, block_t* block) {
    memcpy(fs + offset * BLOCK_SIZE, block, BLOCK_SIZE);
    return 0;
}

int main() {
    printf("[INFO] Load %ld kernel binary files\n", EGOS_BIN_NUM);
    for (uint i = 0; i < EGOS_BIN_NUM; i++) {
        int sz = load_file(egos_binaries[i], exec + i * EGOS_BIN_MAX_NBYTE);
        if (sz < 0) return 1;
        printf("[INFO] Load %s: %d bytes\n", egos_binaries[i], sz);
    }

    printf("MKFS is using *%s*\n", FILESYS == 0 ? "mydisk" : "treedisk");

    struct inode_store ramdisk = {
        .read = ramread,
        .write = ramwrite,
        .getsize = getsize,
        .setsize = setsize
    };

    if (FILESYS == 0) assert(mydisk_create(&ramdisk, 0, NINODES) >= 0);
    else assert(treedisk_create(&ramdisk, 0, NINODES) >= 0);

    inode_intf filesys = (FILESYS == 0)
                         ? mydisk_init(&ramdisk, 0)
                         : treedisk_init(&ramdisk, 0);

    block_t blk;

    /* Write inodes 0–5 from contents[] */
    for (uint ino = 0; ino < BIN_DIR_INODE; ino++) {
        memset(&blk, 0, BLOCK_SIZE);
        memcpy(&blk, contents[ino], strlen(contents[ino]));
        printf("[INFO] Load ino=%d, %ld bytes\n", ino, strlen(contents[ino]));
        filesys->write(filesys, ino, 0, &blk);
    }

    /* Write inode 6 (/bin directory) initially */
    memset(&blk, 0, BLOCK_SIZE);
    memcpy(&blk, contents[BIN_DIR_INODE], strlen(contents[BIN_DIR_INODE]));
    filesys->write(filesys, BIN_DIR_INODE, 0, &blk);

    /* -------------------------------------------------------------- */
    /* Load user applications (ELF files) into inodes starting at 7   */
    /* -------------------------------------------------------------- */

    uint app_ino = BIN_DIR_INODE + 1; /* start at 7 */

    DIR* dp = opendir("../build/release/user");
    if (!dp) {
        printf("[ERROR] Cannot open ../build/release/user\n");
        return 1;
    }

    for (struct dirent* ep = readdir(dp); ep != NULL; ep = readdir(dp)) {
        if (!strstr(ep->d_name, ".elf")) continue;

        snprintf(tmp, sizeof(tmp), "../build/release/user/%s", ep->d_name);
        int file_size = load_file(tmp, inode_buf);
        if (file_size < 0) return 1;

        printf("[INFO] Load ino=%d, %s: %d bytes\n",
               app_ino, ep->d_name, file_size);

        for (uint b = 0; b * BLOCK_SIZE < (uint)file_size; b++) {
            memset(&blk, 0, BLOCK_SIZE);
            int copy = BLOCK_SIZE;
            if ((b + 1) * BLOCK_SIZE > (uint)file_size)
                copy = file_size - b * BLOCK_SIZE;
            memcpy(&blk, inode_buf + b * BLOCK_SIZE, copy);
            filesys->write(filesys, app_ino, b, &blk);
        }

        /* Add app to /bin directory listing */
        char name_no_ext[128];
        strncpy(name_no_ext, ep->d_name, sizeof(name_no_ext));
        name_no_ext[strlen(name_no_ext) - 4] = '\0'; /* remove .elf */

        char entry[64];
        snprintf(entry, sizeof(entry), "%s%4d ", name_no_ext, app_ino);
        strncat(bin_dir, entry, sizeof(bin_dir) - strlen(bin_dir) - 1);

        app_ino++;
    }
    closedir(dp);

    /* Update /bin directory (inode 6) */
    memset(&blk, 0, BLOCK_SIZE);
    memcpy(&blk, bin_dir, strlen(bin_dir));
    filesys->write(filesys, BIN_DIR_INODE, 0, &blk);

    /* -------------------------------------------------------------- */
    /* Load testfile.txt into the next free inode                     */
    /* -------------------------------------------------------------- */

    int test_size = load_file("testfile.txt", inode_buf);
    if (test_size >= 0) {
        uint test_ino = app_ino; /* NEXT FREE INODE AFTER APPS */

        printf("[INFO] Loading testfile.txt into inode %d (%d bytes)\n",
               test_ino, test_size);

        for (uint b = 0; b * BLOCK_SIZE < (uint)test_size; b++) {
            memset(&blk, 0, BLOCK_SIZE);
            int copy = BLOCK_SIZE;
            if ((b + 1) * BLOCK_SIZE > (uint)test_size)
                copy = test_size - b * BLOCK_SIZE;
            memcpy(&blk, inode_buf + b * BLOCK_SIZE, copy);
            filesys->write(filesys, test_ino, b, &blk);
        }

        /* Update /home/yunhao directory (inode 2) */
        char home_entry[BLOCK_SIZE];
        snprintf(home_entry, sizeof(home_entry),
                 "./   2 ../   1 README   5 testfile.txt   %4d ",
                 test_ino);

        memset(&blk, 0, BLOCK_SIZE);
        memcpy(&blk, home_entry, strlen(home_entry));
        filesys->write(filesys, 2, 0, &blk);

        app_ino++;
    } else {
        printf("[WARN] testfile.txt not found, skipping.\n");
    }

    /* -------------------------------------------------------------- */
    /* Build disk.img, fpgaROM.bin and qemuROM.bin                    */
    /* -------------------------------------------------------------- */

    int fd = open("disk.img", O_CREAT | O_WRONLY, 0666);
    write(fd, exec, SIZE_2MB);
    write(fd, fs, SIZE_2MB);
    close(fd);

    fd = open("fpgaROM.bin", O_CREAT | O_WRONLY, 0666);
    load_file(CPU_BIN_FILE, vexriscv);
    write(fd, vexriscv, SIZE_2MB * 2);
    write(fd, exec, SIZE_2MB);
    write(fd, fs, SIZE_2MB);
    close(fd);

    fd = open("qemuROM.bin", O_CREAT | O_WRONLY, 0666);
    write(fd, exec, SIZE_2MB);
    for (uint i = 0; i < 15; i++) write(fd, fs, SIZE_2MB);
    close(fd);

    printf("[INFO] Finish making the image files\n");
    return 0;
}
