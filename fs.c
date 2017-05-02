
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

// FREE BLOCK BITMAP
// ARRAY OF INTS, 1 if used 0 if unused
// Everytime a system reboots, it needs to scan through and recreate the bitmap

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


//////////// FUNCTIONS ////////////

int fs_format()
{
	return 0;
}

void fs_debug()
{
	union fs_block block;

	disk_read(0,block.data);
	printf("superblock:\n");
	if(block.super.magic == FS_MAGIC){
		printf("    magic number is valid\n");
	} else{
		printf("    magic number is not valid\n");
	}
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	int i = 0;
	int j = 0;
	int k = 0;
	union fs_block it_block;
	union fs_block tmp_block;

	// INODE HANDLER //
	for(i = 0; i<block.super.ninodeblocks; i++){ // Iterates through all inode blocks
		disk_read(i,it_block.data);
		for(j = 0; j<128; j++){ // scans 128 inodes per block
			if(it_block.inode[j].isvalid ==1){
				printf("inode %d:\n",j*i);
				printf("    size: %d bytes\n",it_block.inode[j].size);
				printf("    direct blocks:");
				for(k=0; k<4; k++){ // direct blocks
				//	if(it_block.inode[j].direct[k] > 0){
						printf(" %d",it_block.inode[j].direct[k]);
				//	}
				}
				printf("\n");
				if(it_block.inode[j].indirect > 0){ // indirect block
					printf("    indirect block: %d\n",it_block.inode[j].indirect);
					disk_read(it_block.inode[j].indirect,tmp_block.data);
					printf("    indirect data blocks:");
					for(k=0; k<1024; k++){
						if(tmp_block.pointers[k] > 0){
							printf(" %d",tmp_block.pointers[k]);
						}
					}
					printf("\n");
				}
			}
		}
	}
}

int fs_mount()
{
	return 0;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
