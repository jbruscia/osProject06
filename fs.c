
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

int numBlocks = 0;
int iBlocks = 0;
int numNodes = 0;
int *free_list;
int free_size;
int mountedOrNah = 0;

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
    //create a new filesystem, destroying any data already present
    //set aside ten percent of the blocks for inodes, clears the inode table, and writes the super block
    //returns one on success, zero otherwise
    if(mountedOrNah == 1){
        printf("File system cannot format an already-mounted disk. Format failed!\n");
        return 0;
    }
    union fs_block block;

    disk_read(0, block.data);

    int k, i, j;
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    for(k = 1; k <= iBlocks; k += 1){
        disk_read(k, block.data);
        for(i = 0; i < INODES_PER_BLOCK; i += 1){
            if(block.inode[i].isvalid){
                if(block.inode[i].indirect) {
                    block.inode[i].indirect = 0;
                }
                for(j = 0; j < POINTERS_PER_INODE; j += 1){
                    if(block.inode[i].direct[j]){
                        block.inode[i].direct[j] = 0;
                    }
                }
                block.inode[i].isvalid = 0;
            }
        }
        disk_write(k, block.data);
    }
    for(k = iBlocks + 1; k < numBlocks; k += 1){
        disk_read(k, block.data);
        memset(&block.data[0], 0, sizeof(block.data));
        disk_write(k, block.data); 
    }
    //creating a new superblock
    struct fs_superblock newSuper;
    newSuper.magic = FS_MAGIC;
    newSuper.nblocks = disk_size();
    int newInodeNum = newSuper.nblocks / 10 + 1;
    if(newInodeNum < 1){
        printf("ERROR: ninodeblocks cannot be less than 1!\n");
        return 0;
    }
    newSuper.ninodeblocks = newInodeNum;
    newSuper.ninodes = INODES_PER_BLOCK;
    block.super = newSuper;

    disk_write(0, block.data);
    mountedOrNah = 0;

    return 1;
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
    int k,i,j;
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    int blockCount = 0;
    for (k = 1; k <= iBlocks; k += 1)
    {
        //printf("block loop \n");
        disk_read(k,block.data);
        for (i = 0; i < INODES_PER_BLOCK; i += 1, blockCount += 1) {
            if(block.inode[i].isvalid) {
                printf("inode %d:\n",blockCount);
                printf("    size: %d bytes\n", block.inode[i].size);
                printf("    direct blocks: ");
                for (j = 0; j < POINTERS_PER_INODE; j += 1) {
                    //printf("each block: %d\n", block.inode[i].direct[j]);
                    if (block.inode[i].direct[j]) {
                        printf("%d ", block.inode[i].direct[j]);
                    }
                }
                printf("\n");

                if(block.inode[i].indirect) {
                    printf("    indirect block: %d\n",block.inode[i].indirect);

                    disk_read(block.inode[i].indirect,block.data);
                    printf("    indirect data blocks: ");

                    for (j = 0; j < POINTERS_PER_BLOCK; j += 1){
                        if(block.pointers[j]) printf("%d ", block.pointers[j]);
                    }
                    disk_read(k,block.data);
                    printf("\n");
                }
            }
        }

    }

}
/*
void fill_free_list(){
    union fs_block block;
    disk_read(0, block.data);
    free_list = malloc(sizeof(int) * block.super.nblocks);
    free_size = block.super.nblocks;
    for(i = 0; i < free_size; i += 1){
        free_list[i] = 0;
    }
}
*/


int fs_mount() {
    //Examine the disk for a filesystem. If one is present, read the superblock, build a free block bitmap, and prepare the filesystem for use
    //return one on success, zero otherwise
    union fs_block block;
    disk_read(0,block.data);
    if(block.super.magic != FS_MAGIC){
        return 0;
    }
    free_list = malloc(sizeof(int) * block.super.nblocks);
    int k, i, j;
    numBlocks = block.super.nblocks;
    free_size = block.super.nblocks;
    for(i = 0; i < free_size; i += 1){
        free_list[i] = 0;
    }
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    for(k = 1; k <= iBlocks; k += 1){
        disk_read(k, block.data);
        for(i = 0; i < INODES_PER_BLOCK; i += 1){
            if(block.inode[i].isvalid){
                for(j = 0; j < POINTERS_PER_INODE; j += 1){
                    if(block.inode[i].direct[j]){
                        free_list[block.inode[i].direct[j]] = 1; 
                    }
                }
                if(block.inode[i].indirect){
                    for(j = 0; j < POINTERS_PER_BLOCK; j += 1){
                        if(block.pointers[j]){
                            free_list[block.pointers[j]] = 1;
                        }
                    }
                }
            }
        }
    }
    mountedOrNah = 1;
    return 1;
}

int fs_create() {
    //Create a new inode of zero length
    //return the (positive) inumber on success, on failure return 0
    union fs_block block;
    disk_read(0,block.data);
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    int i, k, blockCount = 0;
    struct fs_inode newInode;
    newInode.isvalid = 1;
    newInode.size = 0;
    for(i = 0; i < POINTERS_PER_INODE; i += 1){
        newInode.direct[i] = 0;
    }
    newInode.indirect = 0;
    for(k = 1; k <= iBlocks; k+=1){
        disk_read(k, block.data);        
        for(i = 1; i < INODES_PER_BLOCK; i += 1){
            if(!block.inode[i].isvalid) {                
                block.inode[i] = newInode;
                disk_write(k, block.data);
                return i + INODES_PER_BLOCK * blockCount;
            }
        }
        blockCount++;
    }
    return 0;
}

int fs_delete( int inumber ) {
    //Delete the inode indicated by the inumber. Release all data and indirect blocks assigned to this inode, returning them to the free block map
    //on success return 1, on failure return 0
    union fs_block block;
    disk_read(0, block.data);
    if(inumber < 1 || inumber > INODES_PER_BLOCK * block.super.ninodeblocks){ //ADD THE CASE THAT IT'S TOO HIGH
        printf("Your input number is invalid!\n");
        return 0;
    }
    int j;
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    disk_read((inumber / INODES_PER_BLOCK) + 1, block.data);
    for(j = 0; j < POINTERS_PER_INODE; j += 1){
        free_list[block.inode[inumber % INODES_PER_BLOCK].direct[j]] = 0;
        block.inode[inumber % INODES_PER_BLOCK].direct[j] = 0;
    }
    block.inode[inumber % INODES_PER_BLOCK].indirect = 0;
    block.inode[inumber % INODES_PER_BLOCK].isvalid = 0;
    disk_write((inumber / INODES_PER_BLOCK) + 1, block.data);
    return 1;
}

int fs_getsize( int inumber ) {
    //return the logical size of the given inode in bytes. Note that zero is a valid logical size for an inode
    //on failure, return -1
    union fs_block block;
    disk_read(0, block.data);
    if(inumber < 1 || inumber > INODES_PER_BLOCK * block.super.ninodeblocks){ //ADD THE CASE THAT IT'S TOO HIGH
        printf("Your input number is invalid!\n");
        return -1;
    }
    int j;
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    disk_read((inumber / INODES_PER_BLOCK) + 1, block.data);
    return block.inode[inumber % INODES_PER_BLOCK].size;
}

int fs_read( int inumber, char *data, int length, int offset ) {
    //Read data from a valid inode. Copy "length" bytes from the inode into the "data" pointer starting at "offset" bytes
    //Allocate any necessary direct and indirect blocks in the process. Return the number of bytes actually written
    //The number of bytes actually written could be smaller than the number of bytes requested, perhaps if the disk becomes full
    //If the given number is invalid, or any other error is encoutnered, return 0
    union fs_block block;
    disk_read(0, block.data);
    if(inumber < 1 || inumber > INODES_PER_BLOCK * block.super.ninodeblocks){ //ADD THE CASE THAT IT'S TOO HIGH
        printf("Your input number is invalid!\n");
        return -1;
    }
    int j;
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;


    return 0;
}

int fs_write( int inumber, const char *data, int length, int offset ) {
    return 0;
}
//QUESTIONS
//where should the free_list be initialized?
//Does delete mean we need to wipe the data or can we remove the block from the free list?
//Do the inodes go to 1 to 384 or 1 to 128 and reset?
