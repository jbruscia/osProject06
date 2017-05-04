
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

int numBlocks = 0; //number of blocks on disk_read
int iBlocks = 0; //number of blocks allocated for inodes
int numNodes = 0; //number of inodes
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
    //clear inode table
    for(k = 1; k <= iBlocks; k += 1){
        disk_read(k, block.data);
        for(i = 0; i < INODES_PER_BLOCK; i += 1){ //go through each inode, clear map
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
    //free the rest of the data blocks
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
    //update block with new info
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
    //display super block info
    printf("magic number is valid \n");
    printf("superblock:\n");
    printf("    %d blocks on disk\n",block.super.nblocks);
    printf("    %d blocks for inodes\n",block.super.ninodeblocks);
    printf("    %d inodes total\n",block.super.ninodes);
    
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    
    
    int blockCount = 1;
    double sizeRemaining;
    int k,i,j;
    for (k = 1; k <= iBlocks; k += 1) { //for each inode block
        disk_read(k,block.data);
        for (i = 0; i < INODES_PER_BLOCK; i += 1, blockCount += 1) { //for each inode in block
            if(block.inode[i].isvalid) { //if it is valid print its contents
                printf("inode %d:\n",blockCount - 1);
                printf("    size: %d bytes\n", block.inode[i].size);
                printf("    direct blocks: ");
                for (j = 0; j < POINTERS_PER_INODE; j += 1) {
                    if (block.inode[i].direct[j]) { //if there is a direct block, print it
                        printf("%d ", block.inode[i].direct[j]);
                    }
                }
                printf("\n");
                //for indirect pointers
                if(block.inode[i].size > POINTERS_PER_INODE*DISK_BLOCK_SIZE){
                    sizeRemaining = block.inode[i].size - POINTERS_PER_INODE*DISK_BLOCK_SIZE;
                    printf("    indirect block: %d\n",block.inode[i].indirect);

                    disk_read(block.inode[i].indirect,block.data); //read in indirect data block
                    printf("    indirect data blocks: ");
                    //print all indirect blocks used
                    for (j = 0; j < ceil(sizeRemaining/DISK_BLOCK_SIZE); j += 1){ 
                        if(block.pointers[j]) printf("%d ", block.pointers[j]);
                    }
                    disk_read(k,block.data); //return to inode block to read from
                    printf("\n");
                }
            }
        }

    }

}

int fs_mount() {
    //Examine the disk for a filesystem. If one is present, read the superblock, build a free block bitmap, and prepare the filesystem for use
    //return one on success, zero otherwise
    union fs_block block;
    disk_read(0,block.data);
    if(block.super.magic != FS_MAGIC){
        printf("magic number is invalid\n");
        exit(1);
    }
    free_list = malloc(sizeof(int) * block.super.nblocks); //create free list
    
    
    numBlocks = block.super.nblocks;
    free_size = block.super.nblocks;
    int k, i, j;
    double sizeRemaining;
    for(i = 0; i < free_size; i += 1){ //initialize list to 0
        free_list[i] = 0;
    }
    free_list[0] = 1; //save the super block
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    for(k = 1; k <= iBlocks; k += 1) {
        free_list[k] = 1; //make sure to mark the inode blocks
        disk_read(k, block.data);
        for(i = 0; i < INODES_PER_BLOCK; i += 1){ 
            if(block.inode[i].isvalid){
                //use size to determine number of blocks to mark
                for(j = 0; j < POINTERS_PER_INODE; j += 1){
                    if(block.inode[i].direct[j]){ //mark all of the allocated blocks in map
                        free_list[block.inode[i].direct[j]] = 1; 
                    }
                }
                //if there are indirect blocks
                if(block.inode[i].size > POINTERS_PER_INODE*DISK_BLOCK_SIZE){
                    sizeRemaining = block.inode[i].size - POINTERS_PER_INODE*DISK_BLOCK_SIZE;
                    printf("ran that piece of code\n");
                    free_list[block.inode[i].direct[j]] = 1;
                    disk_read(block.inode[i].indirect, block.data);
                    //loop through number of used indirect blocks
                    for(j = 0; j < ceil(sizeRemaining/DISK_BLOCK_SIZE); j += 1){
                        free_list[block.pointers[j]] = 1;
                    }
                    disk_read(k, block.data);
                }
            }
        }
    }
    mountedOrNah = 1; //we are now mounted! update that 
    return 1;
}

int fs_create() {
    //Create a new inode of zero length
    //return the (positive) inumber on success, on failure return 0
    
    
    union fs_block block;
    disk_read(0,block.data);
    if(block.super.magic != FS_MAGIC){
        printf("magic number is invalid\n");
        exit(1);
    }
    //get your super block variables
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    
    //create a new Inode to be inputted
    struct fs_inode newInode;
    newInode.isvalid = 1;
    newInode.size = 0;
    
    int i, k, blockCount = 0;
    for(i = 0; i < POINTERS_PER_INODE; i += 1){ //initialize all new inode values to 0
        newInode.direct[i] = 0;
    }
    newInode.indirect = 0;
    
    for(k = 1; k <= iBlocks; k+=1){ //for each inode block
        disk_read(k, block.data);        
        if(k != 1){ //if it is not the first block, don't skip the zero index
            for(i = 0; i < INODES_PER_BLOCK; i += 1){
                if(!block.inode[i].isvalid) { //locate the first available inode             
                    block.inode[i] = newInode;
                    disk_write(k, block.data);
                    return i + 0 + INODES_PER_BLOCK * blockCount;
                }
            }
        } else { //this is the first inode block, so inode[0] is not used (inode cannot be 0)
            for(i = 1; i < INODES_PER_BLOCK; i += 1){
                if(!block.inode[i].isvalid) { //locate the first available inode      
                    block.inode[i] = newInode;
                    disk_write(k, block.data);
                    return i + 0 + INODES_PER_BLOCK * blockCount;
                }
            }
        }
        blockCount++; //blockount keeps a running number of what block you are in
                      //it does not get reset when you enter a new inode block
    }
    
    //exiting loop means it couldn't find an open inode
    printf("Unable to create new inode, there are no spaces available.\n");
    return 0;
}

int fs_delete( int inumber ) {
    //Delete the inode indicated by the inumber. Release all data and indirect blocks assigned to this inode, returning them to the free block map
    //on success return 1, on failure return 0
    
    //check to see if mounted
    if(!mountedOrNah) {
        printf("You must mount your file system first\n");
        return 0;
    }
    
    
    union fs_block block;
    disk_read(0, block.data);
    if(inumber < 1 || inumber > INODES_PER_BLOCK * block.super.ninodeblocks){ //ADD THE CASE THAT IT'S TOO HIGH
        printf("Your input number is invalid!\n");
        return 0;
    }
    
    //get your super block variables
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    disk_read((inumber / INODES_PER_BLOCK) + 1, block.data);

    
    //declare other variables
    int inodeIndex = (inumber % INODES_PER_BLOCK);
    int j;
    double sizeRemaining;

    //free the direct blocks
    for(j = 0; j < POINTERS_PER_INODE; j += 1){
        free_list[block.inode[inodeIndex].direct[j]] = 0;
    }

    //check to see if indirect blocks were used
    if(block.inode[inodeIndex].size > POINTERS_PER_INODE*DISK_BLOCK_SIZE){
        //free the indirect block
        free_list[block.inode[inodeIndex].indirect] = 0;
        //free the blocks pointed to by indirect block
        sizeRemaining = block.inode[inodeIndex].size - POINTERS_PER_INODE*DISK_BLOCK_SIZE;
        disk_read(block.inode[inodeIndex].indirect, block.data);
        for(j = 0; j < ceil(sizeRemaining/DISK_BLOCK_SIZE); j += 1){
            free_list[block.pointers[j]] = 0;
        }
    }
    
    //make the inode invalid and set size to zero
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
    if(inumber < 1 || inumber > INODES_PER_BLOCK * block.super.ninodeblocks){
        printf("Your input number is invalid!\n");
        return -1;
    }
    
    //get your super block variables
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;
    
    //read the correct inode block
    disk_read((inumber / INODES_PER_BLOCK) + 1, block.data);
    return block.inode[(inumber % INODES_PER_BLOCK)].size;
}

int fs_read( int inumber, char *data, int length, int offset ) {
    //Read data from a valid inode. Copy "length" bytes from the inode into the "data" pointer starting at "offset" bytes
    //Allocate any necessary direct and indirect blocks in the process. Return the number of bytes actually written
    //The number of bytes actually written could be smaller than the number of bytes requested, perhaps if the disk becomes full
    //If the given number is invalid, or any other error is encoutnered, return 0
    //check to see if mounted
    if(!mountedOrNah) {
        printf("You must mount your file system first\n");
        return 0;
    }
    
    union fs_block block;
    disk_read(0, block.data);
    if(inumber < 1 || inumber > INODES_PER_BLOCK * block.super.ninodeblocks){ //ADD THE CASE THAT IT'S TOO HIGH
        printf("Your input number is invalid!\n");
        return -1;
    }

    //declare variables
    int j,i, amountRead = 0, position = offset;
    int inodeSize;
    int inodeBlockToReadFrom = (inumber / INODES_PER_BLOCK) + 1;
    int numInodePointers;
    int inodeIndex = (inumber % INODES_PER_BLOCK) - 0;

    //get your super block variables
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;

    disk_read(inodeBlockToReadFrom, block.data);

    if(!block.inode[inodeIndex].isvalid) {
        printf("You messed up fam, that inode isn't valid\n");
        return 0;
    }

    inodeSize = block.inode[inodeIndex].size;
    numInodePointers = ceil((double)inodeSize / (double)DISK_BLOCK_SIZE); //calculate how many direct blocks are used
    if (numInodePointers > POINTERS_PER_INODE) numInodePointers = POINTERS_PER_INODE; //max of 5 direct pointers

    if(offset >= inodeSize) {
        printf("The offset is greater than the inode size, there is nothing to read\n");
        return 0;
    }

    //read data from direct pointers
    for (j = 0; j < numInodePointers; j += 1) {
        disk_read(block.inode[inodeIndex].direct[j],block.data);
        if((length+offset) < DISK_BLOCK_SIZE) { //if the read is less than one block
            for (i = offset; i < offset+length; i += 1) { //write the one block and return the amount read
                data[i-offset] = block.data[i];
            }
            amountRead = i-offset;
            return amountRead;
        } else {
            //if the current offset amount is greater than the block size, skip block 
            //and decrement position offset (position represents how much offset has already been accounted for)
            if (position >= DISK_BLOCK_SIZE) {
                position -= DISK_BLOCK_SIZE;
                disk_read(inodeBlockToReadFrom, block.data);
                continue;
            }
            //if there is less than one block worth left available to be read
            if(inodeSize - (offset + amountRead) < DISK_BLOCK_SIZE) {
                for (i = 0; i < inodeSize - (offset + amountRead); i += 1) {
                    data[amountRead + i] = block.data[position + i]; //continue to update data
                }
                amountRead += i;
                return amountRead;
            }
            //if there is less than one block worth requested to be read
            if(amountRead + DISK_BLOCK_SIZE + position >= length) {
                for (i = 0; i < length-amountRead; i += 1) { //read from 0 to how much is left to be read
                    data[amountRead + i] = block.data[position + i];
                }
                amountRead += length-amountRead;
                return amountRead;
            } else { //should be able to do a full read of this block (-minus the offset as represented by position)
                for (i = 0; i < DISK_BLOCK_SIZE-position; i += 1) { 
                    data[amountRead + i] = block.data[i + position];
                }
                position = 0; //offset has been taken care of
                amountRead += DISK_BLOCK_SIZE;
            }

        }
        //finished looping through block, read into inode block again to read next
        disk_read(inodeBlockToReadFrom, block.data);
    }


    //finished direct pointer data, moving on to indirect
    //calculate the number of indirect pointers used, based on size of inode
    int numIndirectPointers = ceil((double)inodeSize / (double)DISK_BLOCK_SIZE) - numInodePointers;
    if(numIndirectPointers == 0) { //if there aren's any used, we are done so return
        return amountRead;
    }
    
    disk_read(inodeBlockToReadFrom, block.data);
    int indirectBlockLocation = block.inode[inodeIndex].indirect;
    disk_read(indirectBlockLocation, block.data);
    
    for (j = 0; j < numIndirectPointers; j += 1) { //looping through indirect pointers
        disk_read(block.pointers[j],block.data);
        if (position >= DISK_BLOCK_SIZE) { //if the offset is still larger than the block 
            position -= DISK_BLOCK_SIZE;
            disk_read(indirectBlockLocation, block.data);
            continue;
        }
        //if there is less than one block worth left available to be read
        if(inodeSize - (offset + amountRead) <= DISK_BLOCK_SIZE) {
            for (i = 0; i < inodeSize - (offset + amountRead); i += 1) {
                data[amountRead + i] = block.data[position + i];
            }
            amountRead += i;
            return amountRead;
        }
        //if there is less than one block worth requested to be read
        if(amountRead + DISK_BLOCK_SIZE + position >= length) {
            for (i = 0; i < length-amountRead; i += 1) {
                data[amountRead + i] = block.data[position + i];
            }
            amountRead += length-amountRead;
            //inodeSize -= length-amountRead;
            return amountRead;
        } else { //should be able to do a full read of this block (-minus the offset as represented by position)
            for (i = 0; i < DISK_BLOCK_SIZE-position; i += 1) {
                data[amountRead + i] = block.data[i + position];
            }
            position = 0;
            amountRead += DISK_BLOCK_SIZE;
        }
        disk_read(indirectBlockLocation, block.data);
    }

    //should never reach here, return fail if you do
    return 0;
}

int fs_write( int inumber, const char *data, int length, int offset ) {
    
    //check to see if mounted
    if(!mountedOrNah) {
        printf("You must mount your file system first\n");
        return 0;
    }
    
    union fs_block block;
    disk_read(0, block.data);
    //check validity of inumber
    if(inumber < 1 || inumber > INODES_PER_BLOCK * block.super.ninodeblocks){ 
        printf("Your input number is invalid!\n");
        return -1;
    }

    //declare variables
    int j,i,k, amountWritten = 0, position = offset;
    int inodeSize, dindex;
    int inodeBlockToReadFrom = (inumber / INODES_PER_BLOCK) + 1;
    int numDirectPointers;
    int inodeIndex = (inumber % INODES_PER_BLOCK) - 0;

    //get superblock information
    numBlocks = block.super.nblocks;
    iBlocks = block.super.ninodeblocks;
    numNodes = block.super.ninodes;

    //read from desired inode
    disk_read(inodeBlockToReadFrom, block.data);
    
    //check inode validity
    if(!block.inode[inodeIndex].isvalid) {
        printf("you messed up fam, that inode isn't valid\n");
        return 0;
    }

    inodeSize = block.inode[inodeIndex].size;
    numDirectPointers = ceil((double)inodeSize / (double)DISK_BLOCK_SIZE);
    if (numDirectPointers > POINTERS_PER_INODE) numDirectPointers = POINTERS_PER_INODE; //cap off the numDirectPointers

    if(!(offset > POINTERS_PER_INODE*DISK_BLOCK_SIZE)) { //if the offset is greating than the bytes in the direct blocks, skip
        for (i = 0; i < POINTERS_PER_INODE; i += 1) {
            if ((i + 1) > numDirectPointers){ //need to allocate a direct pointer
                for (j = 0; j < free_size; j += 1) { //looping through the free list
                    if(free_list[j] == 0) { //is it free or nah?
                        //mark and write
                        free_list[j] = 1;
                        block.inode[inodeIndex].direct[i] = j;
                        disk_write(inodeBlockToReadFrom,block.data);
                        disk_read(inodeBlockToReadFrom,block.data);
                        break;
                    }
                }
                if(j == free_size){
                    //all data blocks full
                    if((offset + amountWritten) > inodeSize) { //checks whether size needs update
                        block.inode[inodeIndex].size = offset + amountWritten;
                        disk_write(inodeBlockToReadFrom, block.data);
                    }
                    return amountWritten;
                }
            }
            if (position > DISK_BLOCK_SIZE) { //if the offset calls for skipping this block
                position -= DISK_BLOCK_SIZE;
                continue;
            }
            dindex = block.inode[inodeIndex].direct[i]; //saving the index of the direct pointer
            disk_read(dindex, block.data);
            for (k = position; k < DISK_BLOCK_SIZE; k += 1) {
                block.data[k] = data[amountWritten]; //writes the data
                amountWritten++; position--; //increment/decrement tracking info
                if (amountWritten >= length ) { //if the amountWritten is greater than or equal to the amount asked for, you're done!
                    disk_write(dindex,block.data);  //write the datat
                    disk_read(inodeBlockToReadFrom,block.data); //read from original inode
                    if((offset + amountWritten) > inodeSize) { //do you need to update size?
                        block.inode[inodeIndex].size = offset + amountWritten;
                        disk_write(inodeBlockToReadFrom, block.data);
                    }
                    return amountWritten;                    
                }
            }
            position = 0;
            disk_write(dindex,block.data);
            disk_read(inodeBlockToReadFrom,block.data);
        }
    } else { //decrement the position the size of the direct pointers
        position -= (POINTERS_PER_INODE*DISK_BLOCK_SIZE);
    }

    int numIndirectPointersAllocated = ceil((double)inodeSize / (double)DISK_BLOCK_SIZE) - numDirectPointers; //finding the amount of indirect pointers
    disk_read(inodeBlockToReadFrom, block.data);
    int indirectBlockLocation; //location of indirect block
    if(numIndirectPointersAllocated > 0){ //if there's one allocated
        indirectBlockLocation = block.inode[inodeIndex].indirect;
    }
    else {
        for(j = 0; j < free_size; j += 1){
            if(free_list[j] == 0){ //this block is free!
                //allocating an indirect block
                free_list[j] = 1;
                indirectBlockLocation = j;
                block.inode[inodeIndex].indirect = j;
                disk_write(inodeBlockToReadFrom, block.data);
                break;
            }
        }
        if (j == free_size) {
            //all data blocks full
            if((offset + amountWritten) > inodeSize) { //checks whether size needs update
                block.inode[inodeIndex].size = offset + amountWritten;
                disk_write(inodeBlockToReadFrom, block.data);
             }
             return amountWritten;           
        }

    }
    for (i = 0; i < POINTERS_PER_BLOCK; i += 1){
        //allocation check
        if ((i+1) > numIndirectPointersAllocated){ //need to allocate
            for(j = 0; j < free_size; j += 1){
                if(free_list[j] == 0){ //this block is free!
                    free_list[j] = 1;
                    disk_read(indirectBlockLocation, block.data);
                    block.pointers[i] = j;
                    disk_write(indirectBlockLocation, block.data);
                    disk_read(inodeBlockToReadFrom, block.data);
                    break;
                }
            }
            if(j == free_size){
                //all data blocks are full!
                printf("All data blocks are full! The entire file was not able to be written\n");
                if(offset + amountWritten > inodeSize){
                    block.inode[inodeIndex].size = offset + amountWritten;
                    disk_write(inodeBlockToReadFrom, block.data);
                }
                return amountWritten;
            }
        }
        //offset check
        if(position > DISK_BLOCK_SIZE) {
            position -= DISK_BLOCK_SIZE;
            continue;
        }
        //get back to the correct block
        disk_read(indirectBlockLocation, block.data);
        dindex = block.pointers[i]; 
        disk_read(dindex, block.data);
        //write the data
        for(k = position; k < DISK_BLOCK_SIZE; k += 1){
            block.data[k] = data[amountWritten];
            amountWritten++; position--;
            if(amountWritten >= length){ //we are done writing
                disk_write(dindex, block.data);
                disk_read(inodeBlockToReadFrom, block.data);
                if(offset + amountWritten > inodeSize){ //update size
                    block.inode[inodeIndex].size = offset + amountWritten;
                    disk_write(inodeBlockToReadFrom, block.data);
                }
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
    return amountWritten; //all done, return
}
