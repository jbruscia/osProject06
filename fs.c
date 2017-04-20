
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct fs_superblock {
    int magic;
    int nblocks;
    int ninodeblocks;
    int ninodes;
};

struct fs_inode {
    int isvalid;
    int size;
    int direct[POINTERS_PER_INODE];
    int indirect;
};

union fs_block {
    struct fs_superblock super;
    struct fs_inode inode[INODES_PER_BLOCK];
    int pointers[POINTERS_PER_BLOCK];
    char data[DISK_BLOCK_SIZE];
};

int fs_format() {
    return 0;
}

void fs_debug() {
    union fs_block block;

    disk_read(0,block.data);

    if(block.super.magic != FS_MAGIC) {
        printf("magic number is invalid\n");
        exit(1);
    }
    printf("magic number is valid \n");
    printf("superblock:\n");
    printf("    %d blocks on disk\n",block.super.nblocks);
    printf("    %d blocks for inodes\n",block.super.ninodeblocks);
    printf("    %d inodes total\n",block.super.ninodes);
    printf("tabbin");

    int i;
    for (i = 0; i < INODES_PER_BLOCK; i += 1) {
        if(block.inode[i].isvalid) {
            printf("inode %d:\n",i+1);
            printf("    size: %d bytes\n", block.inode[i].size);
            //printf("    direct blocks: ")
        }
    }
}

int fs_mount() {
    return 0;
}

int fs_create() {
    return 0;
}

int fs_delete( int inumber ) {
    return 0;
}

int fs_getsize( int inumber ) {
    return -1;
}

int fs_read( int inumber, char *data, int length, int offset ) {
    return 0;
}

int fs_write( int inumber, const char *data, int length, int offset ) {
    return 0;
}
