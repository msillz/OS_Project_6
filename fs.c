
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

bool ISMOUNT = false;

// FREE BLOCK BITMAP
int *bitmap; // ARRAY OF INTS, 1 if used 0 if unused
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


//////////// FUNCTIONS /////////////

int fs_format()
{
	if(ISMOUNT){
		printf("Disk already mounted. Please de-mount before attempting to format.\n");
		return 0;
	}

	int nblocks = disk_size();
	/*if((nblocks % 10) != 0){
		ninodeblocks = (nblocks/10)+1;
	} else{
		ninodeblocks = nblocks/10;
	}*/
	
	int ninodeblocks = ceil(nblocks/10);

	union fs_block block;

	int nodesToZero;
	disk_read(0,block.data);
	nodesToZero = block.super.ninodeblocks;

	block.super.magic = FS_MAGIC;
	block.super.nblocks = nblocks;
	block.super.ninodeblocks = ninodeblocks;
	block.super.ninodes = ninodeblocks*128;

	disk_write(0,block.data);

	int i,j,k; // sets all the inode valid bits to 0
	for(i=1;i<=nodesToZero;i++){
		for(j=0;j<128;j++){
			if(i>1 || (j!=0 && i==1)){
				block.inode[j].isvalid = 0;
				block.inode[j].size = 0;
				for(k=0;k<5;k++){
					block.inode[j].direct[k] = 0;
				}
				block.inode[j].indirect = 0;
			}
		}
		disk_write(i,block.data);
	}

	return 1;
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
		return;
	}

	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d blocks for inodes\n",block.super.ninodeblocks);
	printf("    %d inodes total\n",block.super.ninodes);

	int i = 0;
	int j = 0;
	int k = 0;
	union fs_block it_block;
	union fs_block tmp_block;

	// INODE HANDLER //
	for(i = 1; i<=block.super.ninodeblocks; i++){ // Iterates through all inode blocks
		disk_read(i,it_block.data);
		for(j = 0; j<128; j++){ // scans 128 inodes per block
			if(it_block.inode[j].isvalid == 1){
				printf("inode %d:\n",j+((i-1)*128)); // check this
				printf("    size: %d bytes\n",it_block.inode[j].size);
				printf("    direct blocks:");
				// ceiling of (size / 4096) is the limit rather than 5, print all direct blocks
				for(k=0; k<5; k++){ // direct blocks
					if(it_block.inode[j].direct[k] > 0){ // CHANGE THIS
						printf(" %d",it_block.inode[j].direct[k]);
					}
				}
				printf("\n");
				if(it_block.inode[j].indirect > 0){ // indirect block
					printf("    indirect block: %d\n",it_block.inode[j].indirect);
					disk_read(it_block.inode[j].indirect,tmp_block.data);
					printf("    indirect data blocks:");
					for(k=0; k<1024; k++){
						if(tmp_block.pointers[k] > 0){ // THEY ARE ALL 0
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
	union fs_block block;
	union fs_block it_block;
	union fs_block tmp_block;

	disk_read(0,block.data);
	if(block.super.magic != FS_MAGIC){
		printf("Not a valid filesystem, failed to mount.\n");
		return 0;
	}

	bitmap = (int *) malloc(sizeof(int)*block.super.nblocks); // WHAT IF WE DEMOUNT? NEEDS TO BE 
	//FREED
	int i, j, k;
	bitmap[0] = 1;
	for(i = 1; i<=block.super.ninodeblocks; i++){ // Iterates through all inode blocks
		disk_read(i,it_block.data);
		for(j = 0; j<128; j++){ // scans 128 inodes per block
			if(it_block.inode[j].isvalid ==1){ // if there is a valid inode in a block
				bitmap[i] = i;
				for(k=0; k<5; k++){ // direct blocks
					if(it_block.inode[j].direct[k] > 0){ // CHANGE THIS
						bitmap[it_block.inode[j].direct[k]] = it_block.inode[j].direct[k];
					}
				}

				if(it_block.inode[j].indirect > 0){ // indirect block
					bitmap[it_block.inode[j].indirect] = it_block.inode[j].indirect;
					disk_read(it_block.inode[j].indirect,tmp_block.data);
					for(k=0; k<1024; k++){
						if(tmp_block.pointers[k] > 0){ // THEY ARE ALL 0
							bitmap[tmp_block.pointers[k]] = tmp_block.pointers[k];
						}
					}
				}
			}
		}
	}
	for(i = 0; i<block.super.nblocks; i++){
		printf(" %d ",bitmap[i]);
	}
	
	ISMOUNT = true;
	return 1;
}

int fs_create()
{
	union fs_block block;
	disk_read(0,block.data);
	int ninodeblocks = block.super.ninodeblocks;
	int i,j;
	for(i=1; i<=ninodeblocks;i++){
		disk_read(i,block.data);
		for(j=0;j<INODES_PER_BLOCK;j++){
			if ( (i > 1) || (i == 1 && j !=0) ) {
				if(block.inode[j].isvalid == 0){
					block.inode[j].isvalid = 1;
					block.inode[j].size = 0;
					disk_write(i,block.data);
					return ((i-1)*128)+j;
				}
			}
		}
	}
	printf("error: no space in inode blocks\n");
	return 0;
}

int fs_delete( int inumber )
{
	union fs_block block;
	int numInBlock = inumber%128;
	int numBlock = floor(inumber/128)+1;
	disk_read(numBlock,block.data);
	if(block.inode[numInBlock].isvalid == 0)
	{
		return 0;
	}
	block.inode[numInBlock].isvalid = 0;
	block.inode[numInBlock].size = 0;
	int k;
	for(k=0;k<5;k++){
		block.inode[numInBlock].direct[k] = 0;
	}
	block.inode[numInBlock].indirect = 0;
	disk_write(numBlock,block.data);
	return 1;
	// fix the bitmap???

}

int fs_getsize( int inumber )
{
	union fs_block block;
	int numInBlock = inumber%128;
	int numBlock = floor(inumber/128)+1;
	disk_read(numBlock,block.data);
	if(block.inode[numInBlock].size >= 0){
		return block.inode[numInBlock].size;
	} else{
		return -1;
	}
}

int fs_read( int inumber, char *data, int length, int offset )
{
	union fs_block block;
	int numInBlock = inumber%128;
	int numBlock = floor(inumber/128)+1;
	disk_read(numBlock,block.data);

	if(block.inode[numInBlock].isvalid == 0){
		return 0;
	}

	int bytes_Copied = 0;
	int i,j;
	bool first = true;

	for(i=0;i<5;i++){
		if(block.inode[numInBlock].direct[i] > 0){ // if there is a valid direct pointer

			union fs_block direct;
			disk_read(block.inode[numInBlock].direct[i],direct.data); // read in the direct block

			for(j=offset;j<DISK_BLOCK_SIZE;j++){ // for every data byte
				data[bytes_Copied] = direct.data[j]; // copy the data into data[]
				bytes_Copied++; // increment the number of bytes Copied

				if(bytes_Copied == length){ // if we have copied the length requested, return
					return bytes_Copied;
				}

				if(first){ // Ensures offset only applies to the first direct block
					offset = 0;
					first = false;
				}
			}
		}
	}

	if(block.inode[numInBlock].indirect > 0){ // if there is a valid indirect block
		union fs_block indirect;
		disk_read(block.inode[numInBlock].indirect,indirect.data); // open that indirect block
		for(i=0;i<POINTERS_PER_BLOCK;i++){
			if(indirect.pointers[i] > 0){ // if there is a valid indirect pointer
				union fs_block indirectData;
				disk_read(indirect.pointers[i],indirectData.data); // read that data block
				for(j=0;j<DISK_BLOCK_SIZE;j++){
					data[bytes_Copied] = indirectData.data[j];
					bytes_Copied++;
					
					if(bytes_Copied == length){
						return bytes_Copied;
					}
				}
			}
		}
	}
	return bytes_Copied;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
