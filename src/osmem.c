// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../utils/block_meta.h"
#include "../utils/osmem.h"
#include "../utils/printf.h"
#include <stdint.h> //pentru uintptr_t

//long page_size = sysconf(_SC_PAGESIZE); //dimensiune pagina
#include "../tests/snippets/test-utils.h"
// #define METADATA_SIZE		(sizeof(struct block_meta))
// #define MOCK_PREALLOC		(128 * 1024 - METADATA_SIZE - 8)
// #define MMAP_THRESHOLD		(128 * 1024)
// #define NUM_SZ_SM		11
// #define NUM_SZ_MD		6
// #define NUM_SZ_LG		4
// #define MULT_KB			1024

// #define DIE(assertion, call_description)
// 	do {
// 		if (assertion) {										
// 			fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__);
// 			perror(call_description);
// 			exit(errno);
// 		}						
// 	} while (0)

// struct block_meta {
// 	size_t size;
// 	int status;
// 	struct block_meta *prev;
// 	struct block_meta *next;
// };

// /* Block metadata status values */
// #define STATUS_FREE   0
// #define STATUS_ALLOC  1
// #define STATUS_MAPPED 2
//(void *)-1, sbrk
//MAP_FAILED, "mmap"
size_t MMAP_THRESHOLD_FOR_CALLOC = MMAP_THRESHOLD;
size_t alignment = 8;
//sieze_ t aligned = ((operand + (alignment - 1)) & ~(alignment - 1))
//	https://stackoverflow.com/questions/45213511/formula-for-memory-alignment
size_t align_address(size_t operand) {
	size_t alignment = 8;
    return (operand + (alignment - 1)) & ~(alignment - 1);
}
 
struct block_meta *global_base = NULL;
//struct block_meta *global_position = NULL; //doar pt alloc, nu si mmap

void init_block_meta(struct block_meta *block_meta, size_t size, int status, struct block_meta *prev, struct block_meta *next) {
	block_meta->next = next;
	block_meta->prev = prev;
	block_meta->status = status;
	block_meta->size = size;
	//global_position = block_meta;
}

void splitBlock(struct block_meta* meta, size_t size) {
	if (meta->size - size <= METADATA_SIZE) {
		meta->status = STATUS_ALLOC;
		return;
	}
	struct block_meta *block_meta = (struct block_meta *) ((char *)meta + size);
	block_meta->prev = meta;
	block_meta->next = meta->next;
	block_meta->size = meta->size - size;
	block_meta->status = STATUS_FREE;
	meta->next = block_meta;
	meta->status = STATUS_ALLOC;
	meta->size = size;
	//global_position = block_meta;
}

void coalesceBlocks() {
	struct block_meta *block_meta = global_base;
	while(block_meta->next != NULL) {
		if (block_meta->status == STATUS_FREE && block_meta->next->status == STATUS_FREE) {
			//block_meta->size = (size_t)align_address(block_meta->next->size + block_meta->size);
			block_meta->size = block_meta->next->size + block_meta->size;
			if (block_meta->next->next == NULL) {
				block_meta->next = NULL;
			}
			else {
				block_meta->next->next->prev = block_meta;
				block_meta->next = block_meta->next->next;
			}
		} else {block_meta = block_meta->next;}
	}
}

struct block_meta* findBestBlock(size_t size) { //trebuie ca size sa fie deja aliniata si sa includa header
	struct block_meta *block_meta = global_base;
	struct block_meta *best = NULL;
	while(block_meta != NULL) {
		if (block_meta->status == STATUS_FREE && block_meta->size >= size) {
			if(best == NULL || best->size > block_meta->size) {
				best = block_meta;
			}
		}
		if (block_meta->next == NULL) {
			return best;
		}
		block_meta = block_meta->next;
	}
	return best; //verifica daca e NULL da eroarea
}

void heapPreallocation () {
	struct block_meta *block_meta = (struct block_meta*) sbrk(MOCK_PREALLOC + METADATA_SIZE + 8);
	if (block_meta == (void*)-1) {
		DIE (block_meta == (void *)-1, "sbrk");
		return;
	}
	block_meta->size = MOCK_PREALLOC + 8 + METADATA_SIZE;
	block_meta->prev = NULL;
	block_meta->next = NULL;
	block_meta->status = STATUS_FREE;
	global_base = block_meta;
	//global_position = global_base;
}

void *os_malloc(size_t size)
{
    if (size <= 0) {
        return NULL;
    }

    size = align_address(size) + METADATA_SIZE;

    if (size <= (size_t)MMAP_THRESHOLD_FOR_CALLOC) {
		if (global_base == NULL) {
			heapPreallocation();
		}
		struct block_meta *block_meta = NULL;
        coalesceBlocks();
        block_meta = findBestBlock(size);
        if (block_meta != NULL) {
            splitBlock(block_meta, size);
			//AICIICI
			block_meta->status = STATUS_ALLOC;
            return (void*)((char*)block_meta + METADATA_SIZE);
        }
		block_meta = global_base;
		while(block_meta->next != NULL) {
			block_meta = block_meta->next;
		}
		if(block_meta->status == STATUS_ALLOC) {
			struct block_meta *block_meta2 = (struct block_meta*) sbrk(size);
			if (block_meta2 == (void*)-1) {
				DIE (block_meta2 == (void *)-1, "sbrk");
				return NULL;
			}
			block_meta2->size = size;
			block_meta2->prev = block_meta;
			block_meta2->next = NULL;
			block_meta2->status = STATUS_FREE;
			block_meta->next = block_meta2;
			coalesceBlocks();
        	block_meta = findBestBlock(size);
        	splitBlock(block_meta, size);
			block_meta->status = STATUS_ALLOC;
        	return (void*)((char*)block_meta + METADATA_SIZE);
		}
		if(block_meta->status == STATUS_FREE) 
		{
			struct block_meta *block_meta2 = (struct block_meta*) sbrk(size - block_meta->size);
			if (block_meta2 == (void*)-1) {
				DIE (block_meta2 == (void *)-1, "sbrk");
				return NULL;
			}
			block_meta->size = size;
			block_meta->next = NULL;
			block_meta->status = STATUS_ALLOC;
        	return (void*)((char*)block_meta + METADATA_SIZE);
		}

    } else {
        struct block_meta *block_meta = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (block_meta == MAP_FAILED) {
			DIE(block_meta == MAP_FAILED, "mmap");
			return NULL;
		}
        block_meta->prev = NULL;
        block_meta->next = NULL;
        block_meta->size = size;
        block_meta->status = STATUS_MAPPED;
        return (void*)(sizeof(struct block_meta) + (char*)block_meta);
    }
    return NULL;
}


void os_free(void *ptr)
{
	/* TODO: Implement os_free */
	if (ptr == NULL) {
		return;
	}
	struct block_meta *block_meta = (struct block_meta *)((char *)ptr - METADATA_SIZE);
	if (block_meta->status == STATUS_ALLOC) {
		block_meta->status = STATUS_FREE;
		coalesceBlocks();
	} 
	if (block_meta->status == STATUS_MAPPED) {
		block_meta->status = STATUS_FREE;
		munmap(block_meta, block_meta->size);
	}
}

void *os_calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return NULL;
    }
	MMAP_THRESHOLD_FOR_CALLOC = sysconf(_SC_PAGESIZE);
	if (MMAP_THRESHOLD == -1) {
		DIE (MMAP_THRESHOLD == -1, "sysconf");
		return NULL;
	}
	void *pointer = os_malloc(size * nmemb);
	MMAP_THRESHOLD_FOR_CALLOC = MMAP_THRESHOLD;
	memset(pointer, 0, size);
    return pointer;
}


void *os_realloc(void *ptr, size_t size)
{
	/* TODO: Implement os_realloc */
	if (ptr == NULL) {
		return os_malloc(size);
	}
	if (size == 0) {
		os_free(ptr);
		return NULL;
	}
	size_t copy = size;
	coalesceBlocks();
	struct block_meta *block_meta = (struct block_meta *)((char*)ptr - METADATA_SIZE);
	if (block_meta->status == STATUS_FREE) {
		return NULL;
	}
	if (block_meta->status == STATUS_MAPPED) {
		void *pointer = os_malloc(size);
		if (pointer == NULL) {
			DIE(pointer == NULL, "os_malloc");
			return NULL;
		}
		size = align_address(size);
		if (block_meta->size - METADATA_SIZE < size) {memcpy(pointer, ptr, block_meta->size - METADATA_SIZE);}
		else {memcpy(pointer, ptr, size);}
		os_free(ptr);
		return pointer;
	}

	if (block_meta->size >= align_address(size) + METADATA_SIZE) {
		size_t size2 = align_address(size) + METADATA_SIZE;
		if (block_meta->size - size2 <= METADATA_SIZE) {
			return ptr;
		} else {
			splitBlock(block_meta, size + METADATA_SIZE);
			//struct block_meta *block_meta2 = (struct block_meta*)((char*)block_meta + size);
			// block_meta2->next = block_meta->next;
			// block_meta->next = block_meta2;
			// block_meta2->size = block_meta->size - size;
			// block_meta2->prev = block_meta;
			// block_meta2->status = STATUS_FREE;
			// block_meta->size = size;
			return (void*)((char*)block_meta + METADATA_SIZE);
		}
	}
	//if (block_meta->size < size + METADATA_SIZE) {
	size = align_address(size);
	if (block_meta->next == NULL) {
		struct block_meta *block_meta2 = (struct block_meta*)sbrk(size - block_meta->size  + METADATA_SIZE);
		if (block_meta2 == (void*)-1) {
			DIE (block_meta2 == (void *)-1, "sbrk");
			return NULL;
		}
		block_meta->size = size + METADATA_SIZE;
		block_meta->next = NULL;
		block_meta->status = STATUS_ALLOC;
        return (void*)((char*)block_meta + METADATA_SIZE);
	}
	// if (block_meta->next->status == STATUS_FREE && block_meta->size + block_meta->next->size - METADATA_SIZE >= size) {
	// 	if (block_meta->next->next == NULL) {
	// 		block_meta->next = NULL;
	// 	}
	// 	else {
	// 		block_meta->next->next->prev = block_meta;
	// 		block_meta->next = block_meta->next->next;
	// 	}
	// 	block_meta->size = block_meta->next->size + block_meta->size;
	// 	block_meta *block_meta2 = (struct block_meta*)((char*)block_meta + block_meta->size)
	// 	return ptr;
	// }
	void *pointer = os_malloc(copy);
	if (pointer == NULL) {
		DIE(pointer == NULL, "os_realloc");
		return NULL;
	}
	memcpy(pointer, ptr, block_meta->size);
	if(block_meta->status == STATUS_MAPPED) {
		os_free(ptr);
	}
	return pointer;
	return NULL;
}
