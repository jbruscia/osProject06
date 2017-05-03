
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

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
    int blockCount = 1;
    double sizeRemaining;
    for (k = 1; k <= iBlocks; k += 1)
    {
        //printf("block loop \n");
        disk_read(k,block.data);
        for (i = 0; i < INODES_PER_BLOCK; i += 1, blockCount += 1) {
            if(block.inode[i].isvalid) {
                printf("inode %d:\n",i);
                printf("    size: %d bytes\n", block.inode[i].size);
                printf("    direct blocks: ");
                for (j = 0; j < POINTERS_PER_INODE; j += 1) {
                    //printf("each block: %d\n", block.inode[i].direct[j]);
                    if (block.inode[i].direct[j]) {
                        printf("%d ", block.inode[i].direct[j]);
                    }
                }
                printf("\n");

                if(block.inode[i].size > POINTERS_PER_INODE*DISK_BLOCK_SIZE){

                    sizeRemaining = block.inode[i].size - POINTERS_PER_INODE*DISK_BLOCK_SIZE;
                    printf("    indirect block: %d\n",block.inode[i].indirect);

                    disk_read(block.inode[i].indirect,block.data);
                    printf("    indirect data blocks: ");

                    for (j = 0; j < ceil(sizeRemaining/DISK_BLOCK_SIZE); j += 1){
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
    double sizeRemaining;
    numBlocks = block.super.nblocks;
    free_size = block.super.nblocks;
    for(i = 0; i < free_size; i += 1){
        free_list[i] = 0;
    }
    free_list[0] = 1;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    for(k = 1; k <= iBlocks; k += 1) {
        free_list[k] = 1;
        disk_read(k, block.data);
        for(i = 0; i < INODES_PER_BLOCK; i += 1){
            if(block.inode[i].isvalid){
                //use size to determine number of blocks to mark
                for(j = 0; j < POINTERS_PER_INODE; j += 1){
                    if(block.inode[i].direct[j]){
                        free_list[block.inode[i].direct[j]] = 1; 
                    }
                }
                if(block.inode[i].size > POINTERS_PER_INODE*DISK_BLOCK_SIZE){
                    sizeRemaining = block.inode[i].size - POINTERS_PER_INODE*DISK_BLOCK_SIZE;
                    printf("ran that piece of code\n");
                    free_list[block.inode[i].direct[j]] = 1;
                    disk_read(block.inode[i].indirect, block.data);
                    for(j = 0; j < ceil(sizeRemaining/DISK_BLOCK_SIZE); j += 1){
                        free_list[block.pointers[j]] = 1;
                    }
                    disk_read(k, block.data);
                }
            }
        }
    }
    for(i = 0; i < free_size; i += 1){
        printf("i: %d free_list[i]: %d\n",i,free_list[i]);
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
        if(k != 1){
            for(i = 0; i < INODES_PER_BLOCK; i += 1){

                if(!block.inode[i].isvalid) {                
                    block.inode[i] = newInode;
                    disk_write(k, block.data);
                    return i + 0 + INODES_PER_BLOCK * blockCount;
                }
            }
        } else {
            for(i = 1; i < INODES_PER_BLOCK; i += 1){

                if(!block.inode[i].isvalid) {                
                    block.inode[i] = newInode;
                    disk_write(k, block.data);
                    return i + 0 + INODES_PER_BLOCK * blockCount;
                }
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
    double sizeRemaining;
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    disk_read((inumber / INODES_PER_BLOCK) + 1, block.data);

    int inodeIndex = (inumber % INODES_PER_BLOCK) - 0;

    printf("inumber: %d data block: %d",(inodeIndex),(inumber / INODES_PER_BLOCK) + 1 );

    for(j = 0; j < POINTERS_PER_INODE; j += 1){
        free_list[block.inode[inodeIndex].direct[j]] = 0;
        //block.inode[inumber % INODES_PER_BLOCK].direct[j] = 0;
    }

    if(block.inode[inodeIndex].size > POINTERS_PER_INODE*DISK_BLOCK_SIZE){

        sizeRemaining = block.inode[inodeIndex].size - POINTERS_PER_INODE*DISK_BLOCK_SIZE;
        disk_read(block.inode[inodeIndex].indirect, block.data);
        for(j = 0; j < ceil(sizeRemaining/DISK_BLOCK_SIZE); j += 1){
            free_list[block.pointers[j]] = 0;
        }
    }
    disk_read((inumber / INODES_PER_BLOCK) + 1, block.data);
    block.inode[inodeIndex].indirect = 0;
    block.inode[inodeIndex].isvalid = 0;
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
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    disk_read((inumber / INODES_PER_BLOCK) + 1, block.data);
    return block.inode[(inumber % INODES_PER_BLOCK) - 0].size;
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



    int j,i, amountRead = 0, position = offset;
    int inodeSize;
    int inodeBlockToReadFrom = (inumber / INODES_PER_BLOCK) + 1;
    int numInodePointers;



    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    int inodeIndex = (inumber % INODES_PER_BLOCK) - 0;

    disk_read(inodeBlockToReadFrom, block.data);

    if(!block.inode[inodeIndex].isvalid) {
        printf("you messed up fam, that inode isn't valid\n");
        return 0;
    }

    printf("length: %d\n", length);
    inodeSize = block.inode[inodeIndex].size;
    numInodePointers = ceil((double)inodeSize / (double)DISK_BLOCK_SIZE);
    if (numInodePointers > 5) numInodePointers = 5;

    if(offset >= inodeSize) return 0;

    //direct pointers
    printf("numInodePointers: %d\n", numInodePointers);
    for (j = 0; j < numInodePointers; j += 1) {
        printf("Block we are finna enterma: %d\n",block.inode[inodeIndex].direct[j]);
        disk_read(block.inode[inodeIndex].direct[j],block.data);
        if((length+offset) < DISK_BLOCK_SIZE) { //tiny little read
            printf("fam 1\n");
            for (i = offset; i < offset+length; i += 1) {
                data[i-offset] = block.data[i];
            }
            amountRead = i-offset;
            //inodeSize -= i-offset;

            return amountRead;
        } else {
            if (position >= DISK_BLOCK_SIZE) {
                position -= DISK_BLOCK_SIZE;
                printf("position: %d\n", position);
                disk_read(inodeBlockToReadFrom, block.data);
                continue;
            }
            if(inodeSize - (offset + amountRead) < DISK_BLOCK_SIZE) {
                for (i = 0; i < inodeSize - (offset + amountRead); i += 1) {
                    data[amountRead + i] = block.data[position + i];
                }
                amountRead += i;
                return amountRead;
            }
            printf("fam 2\n");
            if(amountRead + DISK_BLOCK_SIZE + position >= length) {
                printf("amount left: %d\n",(length-amountRead));
                for (i = 0; i < length-amountRead; i += 1) {
                    data[amountRead + i] = block.data[position + i];
                }
                amountRead += length-amountRead;
                //inodeSize -= length-amountRead;
                return amountRead;
            } else {
                //if((length-amountRead) <= 0) return (amountRead + DISK_BLOCK_SIZE);
                printf("fam 3 amount written:%d\n", amountRead);

                for (i = 0; i < DISK_BLOCK_SIZE-position; i += 1) {
                    data[amountRead + i] = block.data[i + position];
                }
                position = 0;
                amountRead += DISK_BLOCK_SIZE;
                //inodeSize -= DISK_BLOCK_SIZE;
            }

        }
        disk_read(inodeBlockToReadFrom, block.data);
        printf("looped through\n");
        printf("Block we are finna enterma end: %d\n",block.inode[inodeIndex].direct[j]);
    }
    //printf("words for now: %s\n", data);
    //printf("inidrect block: %d\n",block.inode[inumber % INODES_PER_BLOCK].indirect);

    printf("entering indirect land, amount left: %d\n", length-amountRead);


    int numIndirectPointers = ceil((double)inodeSize / (double)DISK_BLOCK_SIZE);
    disk_read(inodeBlockToReadFrom, block.data);



    int indirectBlockLocation = block.inode[inodeIndex].indirect;

    disk_read(indirectBlockLocation, block.data);
    for (j = 0; j < numIndirectPointers; j += 1) {
        printf("in da loo\n");
        disk_read(block.pointers[j],block.data);
        if (position >= DISK_BLOCK_SIZE) {
            position -= DISK_BLOCK_SIZE;
            printf("position: %d", position);
            disk_read(indirectBlockLocation, block.data);
            continue;
        }
        if(inodeSize - (offset + amountRead) <= DISK_BLOCK_SIZE) {
            for (i = 0; i < inodeSize - (offset + amountRead); i += 1) {
                data[amountRead + i] = block.data[position + i];
            }
            amountRead += i;
            return amountRead;
        }
        printf("fam 2\n");
        if(amountRead + DISK_BLOCK_SIZE + position >= length) {
            printf("amount left: %d amount read: %d inode size %d\n",(length-amountRead),amountRead, inodeSize);
            for (i = 0; i < length-amountRead; i += 1) {
                data[amountRead + i] = block.data[position + i];
            }
            amountRead += length-amountRead;
            //inodeSize -= length-amountRead;
            return amountRead;
        } else {
            //if((length-amountRead) <= 0) return (amountRead + DISK_BLOCK_SIZE);
            printf("fam 3 amount written:%d\n", amountRead);

            for (i = 0; i < DISK_BLOCK_SIZE-position; i += 1) {
                data[amountRead + i] = block.data[i + position];
            }
            position = 0;
            amountRead += DISK_BLOCK_SIZE;
            //inodeSize -= DISK_BLOCK_SIZE;
        }
        disk_read(indirectBlockLocation, block.data);
    }
    printf("looped through\n");

    printf("Block we are finna enterma end: %d",block.inode[inodeIndex].direct[j]);



    return 0;
}

int fs_write( int inumber, const char *data, int length, int offset ) {
    union fs_block block;
    disk_read(0, block.data);
    if(inumber < 1 || inumber > INODES_PER_BLOCK * block.super.ninodeblocks){ //ADD THE CASE THAT IT'S TOO HIGH
        printf("Your input number is invalid!\n");
        return -1;
    }



    int j,i,k, amountWritten = 0, position = offset;
    int inodeSize, dindex;
    int inodeBlockToReadFrom = (inumber / INODES_PER_BLOCK) + 1;
    int numDirectPointers;
    int inodeIndex = (inumber % INODES_PER_BLOCK) - 0;


    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;


    disk_read(inodeBlockToReadFrom, block.data);

    if(!block.inode[inodeIndex].isvalid) {
        printf("you messed up fam, that inode isn't valid\n");
        return 0;
    }

    printf("length: %d\n", length);
    inodeSize = block.inode[inodeIndex].size;
    numDirectPointers = ceil((double)inodeSize / (double)DISK_BLOCK_SIZE);
    if (numDirectPointers > 5) numDirectPointers = 5;

    if(!(offset > POINTERS_PER_INODE*DISK_BLOCK_SIZE)) {
        for (i = 0; i < POINTERS_PER_INODE; i += 1) {
            if ((i+1)>numDirectPointers){
                printf("allocating: %d\n",i+1);
                for (j = 0; j < free_size; j += 1) {
                    if(free_list[j] == 0) {
                        free_list[j] = 1;
                        block.inode[inodeIndex].direct[i] = j;
                        disk_write(inodeBlockToReadFrom,block.data);
                        disk_read(inodeBlockToReadFrom,block.data);
                        break;
                    }
                }
                if(j == free_size){
                    //all data blocks full
                    if((offset + amountWritten) > inodeSize) {
                        block.inode[inodeIndex].size = offset + amountWritten;
                        disk_write(inodeBlockToReadFrom, block.data);
                    }
                    return amountWritten;
                }
            }
            if (position > DISK_BLOCK_SIZE) {
                position -= DISK_BLOCK_SIZE;
                continue;
            }
            dindex = block.inode[inodeIndex].direct[i];
            disk_read(dindex, block.data);
            for (k = position; k < DISK_BLOCK_SIZE; k += 1) {
                block.data[k] = data[amountWritten];
                amountWritten++; position--;
                if (amountWritten >= length ) {
                    disk_write(dindex,block.data);                
                    disk_read(inodeBlockToReadFrom,block.data);
                    printf("\n%d\t%d\t%d\n",offset, amountWritten, inodeSize);
                    if((offset + amountWritten) > inodeSize) {
                        block.inode[inodeIndex].size = offset + amountWritten;
                        printf("%d\n", block.inode[inodeIndex].size);
                        disk_write(inodeBlockToReadFrom, block.data);
                    }
                    return amountWritten;                    
                }
            }
            position = 0;
            disk_write(dindex,block.data);
            disk_read(inodeBlockToReadFrom,block.data);
        }
    }else {
        position -= (POINTERS_PER_INODE*DISK_BLOCK_SIZE);
    }

    printf("famming on some indirection, amount left to write: %d\n", length-amountWritten);
    int numIndirectPointersAllocated = ceil((double)inodeSize / (double)DISK_BLOCK_SIZE) - numDirectPointers;
    disk_read(inodeBlockToReadFrom, block.data);
    printf("1\n");
    int indirectBlockLocation;
    if(numIndirectPointersAllocated > 0){
        indirectBlockLocation = block.inode[inodeIndex].indirect;
    }
    else {
        for(j = 0; j < free_size; j += 1){
            if(free_list[j] == 0){ //this block is free!
                free_list[j] = 1;
                indirectBlockLocation = j;
                block.inode[inodeIndex].indirect = j;
                disk_write(inodeBlockToReadFrom, block.data);
                break;
            }
        }

    }
    for (i = 0; i < POINTERS_PER_BLOCK; i += 1){
        //allocation check
        if ((i+1) > numIndirectPointersAllocated){ //need to allocate
            printf("allocating an indirect pointer\n");
            for(j = 0; j < free_size; j += 1){
                if(free_list[j] == 0){ //this block is free!
                    free_list[j] = 1;
                    disk_read(indirectBlockLocation, block.data);
                    printf("2\n");
                    block.pointers[i] = j;
                    disk_write(indirectBlockLocation, block.data);
                    disk_read(inodeBlockToReadFrom, block.data);
                    printf("3\n");
                    break;
                }
            }
            printf("%d =? %d\t%d\n", j, free_size, amountWritten);
            if(j == free_size){
                //all data blocks are full!
                printf("comparing offset %d  amountWritten: %d to inodeSize %d\n", offset, amountWritten, inodeSize);
                if(offset + amountWritten > inodeSize){
                    block.inode[inodeIndex].size = offset + amountWritten;
                    printf("rewriting size from %d to %d", block.inode[inodeIndex].size, offset+amountWritten);
                    printf("4\n");
                    disk_write(inodeBlockToReadFrom, block.data);
                    printf("4 done\n");
                }
                printf("return 1\n");
                return amountWritten;
            }
        }
        printf("past allocation\n");
        //offset check
        if(position > DISK_BLOCK_SIZE) {
            position -= DISK_BLOCK_SIZE;
            continue;
        }
        printf("5.1\n");
        disk_read(indirectBlockLocation, block.data);
        dindex = block.pointers[i];
        disk_read(dindex, block.data);
        printf("5.2\n");
        for(k = position; k < DISK_BLOCK_SIZE; k += 1){
            block.data[k] = data[amountWritten];
            amountWritten++; position--;
            if(amountWritten >= length){ //we are done writing
                disk_write(dindex, block.data);
                disk_read(inodeBlockToReadFrom, block.data);
                if(offset + amountWritten > inodeSize){
                    printf("rewriting size from %d to %d", block.inode[inodeIndex].size, offset+amountWritten);
                    block.inode[inodeIndex].size = offset + amountWritten;
                    disk_write(inodeBlockToReadFrom, block.data);
                }
                printf("return 2\n");
                return amountWritten;
            }
        }
        position = 0;
        disk_write(dindex, block.data);
        disk_read(inodeBlockToReadFrom, block.data);
    }

    disk_read(inodeBlockToReadFrom, block.data);
    block.inode[inodeIndex].size = offset + amountWritten;
    disk_write(inodeBlockToReadFrom, block.data);
    printf("return 3\n");
    return amountWritten;
}
//QUESTIONS
//where should the free_list be initialized?
//Does delete mean we need to wipe the data or can we remove the block from the free list?
//Do the inodes go to 1 to 384 or 1 to 128 and reset?
