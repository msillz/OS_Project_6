
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

int NUM_BLOCKS;

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
	block.super.ninodes = ninodeblocks*INODES_PER_BLOCK;

	disk_write(0,block.data);

	int i,j,k; // sets all the inode valid bits to 0
	for(i = 1;i <= nodesToZero; i++){
		for(j = 0;j < INODES_PER_BLOCK; j++){
			if(i > 1 || (j !=0 && i == 1)){
				block.inode[j].isvalid = 0;
				block.inode[j].size = 0;
				for(k = 0;k < POINTERS_PER_INODE; k++){
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
	for(i = 1; i <= block.super.ninodeblocks; i++){ // Iterates through all inode blocks
		disk_read(i,it_block.data);
		for(j = 0; j < INODES_PER_BLOCK; j++){ // scans 128 inodes per block
			if(it_block.inode[j].isvalid == 1){
				printf("inode %d:\n",j+((i-1)*INODES_PER_BLOCK)); // check this
				printf("    size: %d bytes\n",it_block.inode[j].size);
				printf("    direct blocks:");
				// ceiling of (size / 4096) is the limit rather than 5, print all direct blocks
				for(k = 0; k < POINTERS_PER_INODE; k++){ // direct blocks
					if(it_block.inode[j].direct[k] > 0){ // CHANGE THIS
						printf(" %d",it_block.inode[j].direct[k]);
					}
				}
				printf("\n");
				if(it_block.inode[j].indirect > 0){ // indirect block
					printf("    indirect block: %d\n",it_block.inode[j].indirect);
					disk_read(it_block.inode[j].indirect,tmp_block.data);
					printf("    indirect data blocks:");
					for(k = 0; k < POINTERS_PER_BLOCK; k++){
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
	if(ISMOUNT == true){ //     UNCOMMENT THIS WE NEED IT DON'T FORGET TO UNCOMMENT THIS WE NEED IT
		printf("Error: disk already mounted\n");
		return 0;
	}

	union fs_block block;
	union fs_block it_block;
	union fs_block tmp_block;

	disk_read(0,block.data);
	if(block.super.magic != FS_MAGIC){
		printf("Error: Not a valid filesystem, failed to mount.\n");
		return 0;
	}
	int ninodes = block.super.ninodeblocks;
	NUM_BLOCKS = block.super.nblocks;
	bitmap = (int *) malloc(sizeof(int)*block.super.nblocks); // WHAT IF WE DEMOUNT? NEEDS TO BE 
	//FREED
	int i, j, k;
	for(i=0;i<ninodes+1;i++){
		bitmap[i] = 1;
	}
	for(i = 1; i<= block.super.ninodeblocks; i++){ // Iterates through all inode blocks
		disk_read(i,it_block.data);
		for(j = 0; j< INODES_PER_BLOCK; j++){ // scans 128 inodes per block
			if(it_block.inode[j].isvalid ==1){ // if there is a valid inode in a block
				bitmap[i] = i;
				for(k = 0; k < POINTERS_PER_INODE; k++){ // direct blocks
					if(it_block.inode[j].direct[k] > 0){ // CHANGE THIS
						bitmap[it_block.inode[j].direct[k]] = it_block.inode[j].direct[k];
					}
				}

				if(it_block.inode[j].indirect > 0){ // indirect block
					bitmap[it_block.inode[j].indirect] = it_block.inode[j].indirect;
					disk_read(it_block.inode[j].indirect,tmp_block.data);
					for(k=0; k<POINTERS_PER_BLOCK; k++){
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
	for(i = 1; i <= ninodeblocks; i++){
		disk_read(i,block.data);
		for(j=0;j<INODES_PER_BLOCK;j++){
			if ( (i > 1) || (i == 1 && j !=0) ) {
				if(block.inode[j].isvalid == 0){
					block.inode[j].isvalid = 1;
					block.inode[j].size = 0;
					disk_write(i,block.data);
					return ((i-1)*INODES_PER_BLOCK)+j;
				}
			}
		}
	}
	printf("Error: no space in inode blocks\n");
	return 0;
}

int fs_delete( int inumber )
{
	union fs_block block;
	int numInBlock = inumber%INODES_PER_BLOCK;
	int numBlock = floor(inumber/INODES_PER_BLOCK)+1;
	disk_read(numBlock,block.data);

	if(block.inode[numInBlock].isvalid == 0)
	{
		return 0;
	}

	block.inode[numInBlock].isvalid = 0;
	block.inode[numInBlock].size = 0;

	int k;

	for(k = 0; k < POINTERS_PER_INODE; k++){
		if(block.inode[numInBlock].direct[k] > 0){
			bitmap[block.inode[numInBlock].direct[k]] = 0;
			block.inode[numInBlock].direct[k] = 0; // direct blocks to 0
		}
	}
	
	if(block.inode[numInBlock].indirect > 0){
		union fs_block indirect;
		disk_read(block.inode[numInBlock].indirect,indirect.data);
		for(k=0;k<POINTERS_PER_BLOCK;k++){
			if( indirect.pointers[k] > 0 ){
				bitmap[indirect.pointers[k]] = 0;
				indirect.pointers[k] = 0;
			}
		}
	}

	block.inode[numInBlock].indirect = 0; // indirect blocks to 0
	disk_write(numBlock,block.data);

	return 1;
	// fix the bitmap???

}

int fs_getsize( int inumber )
{
	union fs_block block;
	int numInBlock = inumber%INODES_PER_BLOCK;
	int numBlock = floor(inumber/INODES_PER_BLOCK)+1;
	disk_read(numBlock,block.data);
	if(block.inode[numInBlock].size >= 0){
		return block.inode[numInBlock].size;
	} else{
		return -1;
	}
}

int fs_read( int inumber, char *data, int length, int offset )
{
	if(ISMOUNT==false){return 0;}
	union fs_block block;
	int numInBlock = inumber%INODES_PER_BLOCK;
	int numBlock = floor(inumber/INODES_PER_BLOCK)+1;
	disk_read(numBlock,block.data);

	if(block.inode[numInBlock].isvalid == 0){
		return 0;
	}

	int bytes_Copied = 0;
	int bytes_Traversed = 0;
	int i,j;

	for(i=0;i<POINTERS_PER_INODE;i++){
		if(block.inode[numInBlock].direct[i] > 0){ // if there is a valid direct pointer

			union fs_block direct;
			disk_read(block.inode[numInBlock].direct[i],direct.data); // read in the direct block

			for(j=0;j<DISK_BLOCK_SIZE;j++){ // for every data byte
				if(bytes_Traversed >= offset){
					data[bytes_Copied] = direct.data[j]; // copy the data into data[]
					bytes_Copied++; // increment the number of bytes Copied
					bytes_Traversed++;

					if(bytes_Copied == length){ // if we have copied the length requested, return
						return bytes_Copied;
					}
				} else{
					bytes_Traversed++;
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
					if(bytes_Traversed >= offset){
						data[bytes_Copied] = indirectData.data[j];
						bytes_Copied++;
						bytes_Traversed++;

						if(bytes_Copied == length){
							return bytes_Copied;
						}
					} else{
						bytes_Traversed++;
					}
				}
			}
		}
	}
	return bytes_Copied;
}

int fs_write( int inumber, const char *data, int length, int offset )
{

	if(ISMOUNT == false){return 0;}

	union fs_block block;

	int numInBlock = inumber%INODES_PER_BLOCK;
	int numBlock = floor(inumber/INODES_PER_BLOCK)+1;
	disk_read(numBlock,block.data);
	
	// printf("Reading the %dth element in block %d\n",numInBlock,numBlock);/////@@@@@

	if(block.inode[numInBlock].isvalid == 0){
		printf("Failed to write to inode %d: inode not valid\n",inumber);
		return 0;
	}

	int bytes_Written = 0;
	int bytes_Traversed = 0;
	int i,j,newBlock;
	bool found_block;


	/* DIRECT */
	for(i=0;i<POINTERS_PER_INODE;i++){ // input data into direct pointers
		// Finds which block using Direct Pointers to write to
		// printf("checking direct pointer %d\n",i); ////////@@@@
		if(block.inode[numInBlock].direct[i] <= 0){ // if direct pointer is invalid
			// printf("    no pointer for the %dth direct pointer\n",i); ///////@@@@@@@@
			found_block = false;
			for(j=0;j<NUM_BLOCKS;j++){ // check the bitmap for a free block
				// printf("        checking bitmap element %d\n",j);//////////@@@@@@@@
				if(bitmap[j] == 0){    // if the block is free
					// printf("            free block %d found\n",j);
					newBlock = j;
					block.inode[numInBlock].direct[i] = newBlock; // sets the direct pointer????????
					// printf("NEWBLOCK ======= %d\n",newBlock);
					// printf("            inode %d direct pointer %d set to %d\n",inumber,i,block.inode[numInBlock].direct[i]);///////@@@@@@@
					found_block = true;
					bitmap[newBlock] = 1;
					break;
				}
			}
			if(!found_block && (i==4)){ // makes sure there is space in the bitmap but checks all inodes
				// printf("        Error: cannot allocate new direct data block, no space\n");
				break; // No more space for direct blocks, goes to the indirect blocks
			} else if(!found_block){
				continue;
			}
		}

		union fs_block direct;
		disk_read(block.inode[numInBlock].direct[i],direct.data); // read in the direct block

		// printf("    WRITE TO NODE:\n");
		for(j=0;j<DISK_BLOCK_SIZE;j++){ // for every data byte
			if(bytes_Traversed >= offset){
				direct.data[j] = data[bytes_Written];//@@@@@@@@@@@@@@@  j  bytes Written @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
				// printf("%c",direct.data[j]);
				bytes_Written++; // increment the number of bytes Copied
				block.inode[numInBlock].size++; // increments the size of the inode
				// printf("% d ",bytes_Written);
				bytes_Traversed++;
				if(bytes_Written >= length){ // if we have copied the length requested, return
					disk_write(block.inode[numInBlock].direct[i],direct.data);
					// printf("Wrote %d bytes : exited in direct because %d = %d\n",bytes_Written,bytes_Written,length);
					disk_write(numBlock,block.data); // writes back the inode
					return bytes_Written;
				}
			} else{
				bytes_Traversed++;
			}
		}
		disk_write(block.inode[numInBlock].direct[i],direct.data);
	}
	// printf("Exited after Direct\n");

	/* INDIRECT */
	// printf("Trying to add data to inode======================\n");
	
	if(block.inode[numInBlock].indirect <= 0){ // if the indirect block is not in use
		// printf("No indirect block associated with this inode\n");
		found_block = false;
		for(j=0;j<NUM_BLOCKS;j++){
			if(bitmap[j]==0){
				// printf("indirect block allocated at block %d\n",j);
				newBlock = j;
				block.inode[numInBlock].indirect = newBlock;
				found_block = true;
				bitmap[newBlock] = 1;
				break;
			}
		}
		if(!found_block){
			printf("Error: cannot allocate new indirect block, not enough space\n");
			return 0;
		}
	}


	union fs_block indirect;
	disk_read(block.inode[numInBlock].indirect,indirect.data); // open that indirect block

	for(i=0;i<POINTERS_PER_BLOCK;i++){
		if(indirect.pointers[i] <= 0){ // if there is an unused pointer
			found_block = false;
			for(j=0;j<NUM_BLOCKS;j++){ // check the bitmap for a free block
				if(bitmap[j] == 0){    // if the block is free
					newBlock = j;
					indirect.pointers[i] = newBlock;
					found_block = true;
					bitmap[newBlock] = 1;
					break;
				}
			}
			if(!found_block && (i==POINTERS_PER_BLOCK-1)){
				// printf("no room in the indirect block");
				return 0;
			} else if(!found_block){
				continue;
			}
		}

		union fs_block indirectData;
		disk_read(indirect.pointers[i],indirectData.data); // read that data block

		for(j=0;j<DISK_BLOCK_SIZE;j++){ // for every data byte
			if(bytes_Traversed >= offset){
				indirectData.data[j] = data[bytes_Written]; ////// j bytes_Written @@@@@@@@@@@@@@@@@@@
				// printf("%c",indirectData.data[j]);
				bytes_Written++; // increment the number of bytes Copied
				block.inode[numInBlock].size++; // increments the size of the inode
				// printf("% d ",bytes_Written);
				bytes_Traversed++;
				if(bytes_Written >= length){ // if we have copied the length requested, return
					disk_write(indirect.pointers[i],indirectData.data); // writes to the data block
					// printf("Wrote %d bytes : exited in direct because %d = %d\n",bytes_Written,bytes_Written,length);
					disk_write(numBlock,block.data); // writes back the inode
					disk_write(block.inode[numInBlock].indirect,indirect.data); // writes to the indirect block
					return bytes_Written;
				}
			} else{
				bytes_Traversed++;
			}
		}
		disk_write(indirect.pointers[i],indirectData.data); // writes to the data block
	}

	return bytes_Written;
}
