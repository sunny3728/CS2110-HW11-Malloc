/*
 * CS 2110 Spring 2017
 * Author: Sunny Qi
 */
#include <stdio.h>
/* we need this for uintptr_t */
#include <stdint.h>
/* we need this for memcpy/memset */
#include <string.h>
/* we need this for my_sbrk */
#include "my_sbrk.h"
/* we need this for the metadata_t struct and my_malloc_err enum definitions */
#include "my_malloc.h"

/* You *MUST* use this macro when calling my_sbrk to allocate the
 * appropriate size. Failure to do so may result in an incorrect
 * grading!
 */
#define SBRK_SIZE 2048

/* This is the size of the metadata struct and canary footer in bytes */
#define TOTAL_METADATA_SIZE (sizeof(metadata_t) + sizeof(int))

/* This is the minimum size of a block in bytes, where it can
 * store the metadata, canary footer, and at least 1 byte of data
 */
#define MIN_BLOCK_SIZE (TOTAL_METADATA_SIZE + 1)

/* Used in canary calcuations. See the "Block Allocation" section of the
 * homework PDF for details.
 */
#define CANARY_MAGIC_NUMBER 0xE629

/* Feel free to delete this (and all uses of it) once you've implemented all
 * the functions
 */
#define UNUSED_PARAMETER(param) (void)(param)

/* Our freelist structure - this is where the current freelist of
 * blocks will be maintained. failure to maintain the list inside
 * of this structure will result in no credit, as the grader will
 * expect it to be maintained here.
 * DO NOT CHANGE the way this structure is declared
 * or it will break the autograder.
 */
metadata_t *freelist;

/* Set on every invocation of my_malloc()/my_free()/my_realloc()/
 * my_calloc() to indicate success or the type of failure. See
 * the definition of the my_malloc_err enum in my_malloc.h for details.
 * Similar to errno(3).
 */
enum my_malloc_err my_malloc_errno;

/* MALLOC
 * See my_malloc.h for documentation
 */
void *my_malloc(size_t size) {
	my_malloc_errno = NO_ERROR;
	//check if size is 0
	if(size == 0) {
		my_malloc_errno = NO_ERROR;
		return NULL;
	}
	//check if size is too big
	if(size + TOTAL_METADATA_SIZE > SBRK_SIZE) {
		my_malloc_errno = SINGLE_REQUEST_TOO_LARGE;
		return NULL;
	}
	//if freelist is null:
	//call sbrk
	if(freelist == NULL) {
		// printf("Uh oh freelist is NULL \n");
		freelist = (metadata_t*)my_sbrk(SBRK_SIZE);
		//check if sbrk failed
		if (freelist == NULL) {
			my_malloc_errno = OUT_OF_MEMORY;
			return NULL;
		}
		freelist->next = NULL;
		freelist->size = SBRK_SIZE;
		freelist->canary = ((uintptr_t) freelist ^ CANARY_MAGIC_NUMBER) - freelist->size;
	}

    //loop through free list for block of size >= size
    metadata_t* cur = freelist;
    metadata_t* prev = NULL;
    while(cur != NULL && cur->size < (size + TOTAL_METADATA_SIZE)) {
    	prev = cur;
    	cur = cur->next;
    }
    //if no block was found:
    //call sbrk
    if(cur == NULL) {
    	// printf("I am about to call sbrk! \n");
    	metadata_t* newBlock = (metadata_t*) my_sbrk(SBRK_SIZE);
    	//check if sbrk failed
		if (newBlock == NULL) {
			my_malloc_errno = OUT_OF_MEMORY;
			return NULL;
		}
		//check to see if any block is adjacent to new block
		metadata_t* cur2 = freelist;
		metadata_t* prev2 = NULL;
		while(cur2 != NULL && (metadata_t*)((uintptr_t)cur2 + cur2->size) != newBlock) {
			prev2 = cur2;
			cur2 = cur2->next;
		}
		//if new block is not adjacent to another block in free list
		//add to end of free list
		if(cur2 == NULL) {
			newBlock->next = NULL;
			newBlock->size = SBRK_SIZE;
			newBlock->canary = ((uintptr_t) newBlock ^ CANARY_MAGIC_NUMBER) - newBlock->size;
			prev2->next = newBlock;
			//do actual malloc
			return my_malloc(size);
		}
    	//if new block is adjacent to another block in free list:
    		//keep pointer to block before adjacent block
    		//merge: update block size, and canaries
    		//prev.next = cur.next and cur.next = null
		else if ((metadata_t*)((uintptr_t)cur2 + cur2->size) == newBlock) {
			// printf("I'm gonna merge!!!!\n");
			cur2->size = cur2->size + SBRK_SIZE;
			cur2->canary = ((uintptr_t) cur2 ^ CANARY_MAGIC_NUMBER) - cur2->size;
			//check if cur2 is not first in freelist
			if (prev2 != NULL) {
				prev2->next = cur2->next;
				//get to end of freelist to add cur2 to end
				metadata_t* tail = cur2;
				while(tail-> next != NULL) {
					tail = tail->next;
				}
				tail->next = cur2;
			} else { //else reassign head of freelist to cur->next and make cu2 last in list
				cur2->next->next = cur2;
				freelist = cur2->next;
			}
			cur2->next = NULL;

			//do actual malloc
			return my_malloc(size);
		}
    }
    //if block_size == size:
    //return block
    else if(cur->size == (size + TOTAL_METADATA_SIZE)) {
    	// printf("I found a perfectly sized block :) \n");
    	//remove cur from freelist, account for if cur is first in freelist
    	if(prev == NULL) {
    		freelist = cur->next;
    	} else {
    		prev->next = cur->next;
    	}
    	//set head canary
    	cur->canary = ((uintptr_t) cur ^ CANARY_MAGIC_NUMBER) - cur->size;
    	//setting the tail canary
    	*((int*)((uint8_t*)cur + cur->size - sizeof(int))) = ((uintptr_t) cur ^ CANARY_MAGIC_NUMBER) - cur->size;
    	return (void*)((uint8_t*)cur + sizeof(metadata_t));
    }
    //if block_size < size + total metadata size + min block size
    //return block
    else if(cur->size < size + TOTAL_METADATA_SIZE + MIN_BLOCK_SIZE) {
    	// printf("I'm using a block that is a little bit too big... \n");
    	// int extra_space = cur->size - (size + TOTAL_METADATA_SIZE);
    	//remove cur from freelist, account for if cur is first in freelist
    	if(prev == NULL) {
    		freelist = cur->next;
    	} else {
    		prev->next = cur->next;
    	}
    	//set head canary
    	cur->canary = ((uintptr_t) cur ^ CANARY_MAGIC_NUMBER) - cur->size;
    	//setting the tail canary
    	*((int*)((uint8_t*)cur + cur->size - sizeof(int))) = ((uintptr_t) cur ^ CANARY_MAGIC_NUMBER) - cur->size;
    	return (void*)((uint8_t*)cur + sizeof(metadata_t));
    }
    //if block_size >= size + total metadata size + min block size:
    //split block, resort second part of block
    else if(cur->size >= size + TOTAL_METADATA_SIZE + MIN_BLOCK_SIZE) {
    	// printf("This world isn't perfect. I need to split a block \n");
    	///save old block size
    	int oldSize = cur->size;
    	//remove cur from freelist, account for if cur is first in freelist
    	if(prev == NULL) {
    		freelist = cur->next;
    	} else {
    		prev->next = cur->next;
    	}
    	//set size for block to return
    	cur->size = size + TOTAL_METADATA_SIZE;
    	//set up head canary
    	cur->canary = ((uintptr_t) cur ^ CANARY_MAGIC_NUMBER) - cur->size;
    	//set a tail canary for block to return
    	*((int*)((uint8_t*)cur + sizeof(metadata_t) + size)) = ((uintptr_t) cur ^ CANARY_MAGIC_NUMBER) - cur->size;

    	//set up block for leftover block
    	metadata_t* newBlock = (metadata_t*)((uint8_t*)cur + TOTAL_METADATA_SIZE + size);
    	newBlock->size = oldSize - cur->size;
    	// printf("Cur size: %d \n", cur->size);
    	// printf("New Block: %d \n",newBlock->size);
    	newBlock->canary = ((uintptr_t) newBlock ^ CANARY_MAGIC_NUMBER) - newBlock->size;
    	//insert block into freelist
    	metadata_t* cur3 = freelist;
    	metadata_t* prev3 = NULL;
    	while(cur3 != NULL && cur3->size < newBlock->size) {
    		prev3 = cur3;
    		cur3 = cur3->next;
    	}
    	//insert at front
    	if (prev3 == NULL) {
    		newBlock->next = freelist;
    		freelist = newBlock;
    	}
    	//insert at back
    	else if(cur3 == NULL) {
    		prev3->next = newBlock;
    		newBlock->next = NULL;
    	}
    	//insert at middle
    	else {
    		prev3->next = newBlock;
    		newBlock->next = cur3;
    	}
    	return (void*)((uint8_t*)cur + sizeof(metadata_t));
    }

    return NULL;

}

/* REALLOC
 * See my_malloc.h for documentation
 */
void *my_realloc(void *ptr, size_t size) {
    UNUSED_PARAMETER(ptr);
    UNUSED_PARAMETER(size);
    return NULL;
}

/* CALLOC
 * See my_malloc.h for documentation
 */
void *my_calloc(size_t nmemb, size_t size) {
    UNUSED_PARAMETER(nmemb);
    UNUSED_PARAMETER(size);
    return NULL;
}

/* FREE
 * See my_malloc.h for documentation
 */
void my_free(void *ptr) {
    UNUSED_PARAMETER(ptr);
    //set error to no
    //check if ptr is null
    //get pointer to metadata (ptr - sizeof(metadata-t*))
    //check head canary
    //check tail canary
    //check left for merging possibility
    	//loop until cur-> next is freed block
    //check right for merging possibility

}
