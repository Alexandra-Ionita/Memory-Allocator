// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include "block_meta.h"

#define META_SIZE (ALIGN(sizeof(struct block_meta)))
#define MMAP_THRESHOLD (128 * 1024)
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define PAGE_SIZE getpagesize()
#define MIN(a, b) ((a < b) ? (a) : (b))

struct block_meta *mmap_blocks;
struct block_meta *brk_blocks;
int first_sbrk;

//This helps me find the last block in the list so I can add a new one after allocate
struct block_meta *find_last_block(struct block_meta *head)
{
	struct block_meta *last = head;

	while (last->next)
		last = last->next;

	return last;
}

//This function splits a block in two so I have one with the needed size and
//one with a free size that I can reuse
struct block_meta *split_blocks(struct block_meta *blocks, size_t size)
{
	struct block_meta *new_block = (struct block_meta *) (((char *) blocks) + META_SIZE + size);

	new_block->status = STATUS_FREE;
	new_block->size = blocks->size - size - META_SIZE;

	new_block->prev = blocks;
	if (blocks->next) {
		blocks->next->prev = new_block;
		new_block->next = blocks->next;

	} else {
		new_block->next = NULL;
	}

	blocks->next = new_block;

	blocks->size = size;
	blocks->status = STATUS_ALLOC;

	return blocks;
}

//In this function I look for a block that is free and its size is the
//smallest one, but bigger than the needed size
struct block_meta *find_best_block(size_t needed_size)
{
	struct block_meta *best_block = NULL;
	struct block_meta *block = brk_blocks;

	while (block) {
		if (block->status == STATUS_FREE && block->size >= needed_size)
			if (best_block == NULL || block->size < best_block->size)
				best_block = block;

		block = block->next;
	}

	return best_block;
}

//Here I coalesce all the adjantance free blocks
void coalesce_blocks(void)
{
	struct block_meta *copy_head = brk_blocks;

	while (copy_head && copy_head->next) {
		if (copy_head->status == STATUS_FREE && copy_head->next->status == STATUS_FREE) {
			copy_head->size += copy_head->next->size + META_SIZE;

			struct block_meta *elim_block = copy_head->next;

			if (elim_block->next) {
				elim_block->next->prev = copy_head;
				copy_head->next = elim_block->next;

			} else {
				copy_head->next = NULL;
			}

		} else {
			copy_head = copy_head->next;
		}
	}
}

//If it is the first time allocating with brk, I allocate a block
//with the maximum size, which is PAGE_SIZE or MMAP_THRESHOLD, it
//depends if I use calloc or malloc, so I can use sbrk less times
struct block_meta *heap_preallocation(int malloc_calloc)
{
	int max_size = PAGE_SIZE;

	if (malloc_calloc == 1)
		max_size = MMAP_THRESHOLD;

	// I set first_sbrk to one to know that I have already used sbrk
	first_sbrk = 1;

	void *request = sbrk(MMAP_THRESHOLD);

	DIE(request == ((void *) -1), "Error at sbrk in heap preallocation\n");

	struct block_meta *block = (struct block_meta *) request;

	block->status = STATUS_ALLOC;
	block->size = max_size - META_SIZE;
	block->next = NULL;
	block->prev = NULL;

	return block;
}

//Here I alocate using sbrk
struct block_meta *brk_alloc(size_t needed_size, int malloc_calloc)
{
	struct block_meta *block = NULL;

	//If I haven't used sbrk before, I preallocate a block with the max size
	if (first_sbrk == 0) {
		return heap_preallocation(malloc_calloc);

	} else {
		//If it is not the first time using sbrk, I coalesce the free
		//adjancent blocks, than I look for the best fit
		coalesce_blocks();
		block = find_best_block(needed_size);

		//If I find the best fit, I split the block so I do not allocate more
		//than I need
		if (block != NULL) {
			if (block != NULL && block->size >= needed_size + META_SIZE + ALIGN(1))
				block = split_blocks(block, needed_size);

			block->status = STATUS_ALLOC;

		} else {
			// If I do not find a best fit, it means I have to use sbrk to
			//allocate a new block, so I look for the last block in the
			//list in order to add a new block to it or expand it
			struct block_meta *last_block = find_last_block(brk_blocks);

			//If the last block has free status it means I can expand it
			if (last_block->status == STATUS_FREE) {
				void *ptr = (void *)sbrk(needed_size - last_block->size);

				DIE(ptr == (void *) -1, "Eroor at sbrk in heap preallocation\n");

				last_block->status = STATUS_ALLOC;
				last_block->size = needed_size;
				block = last_block;

			} else {
				//If not, I create a new block in the list
				void *request = (void *)sbrk(needed_size + META_SIZE);

				DIE(request == (void *) -1, "Error at sbrk in heap preallocation\n");

				block = (struct block_meta *) request;

				block->size = needed_size;
				block->status = STATUS_ALLOC;
				block->next = NULL;
				block->prev = last_block;
				last_block->next = block;
			}
		}
	}

	return block;
}

//In this function I allocate a block using mmap
struct block_meta *mmap_alloc(size_t needed_size)
{
	void *request = mmap(NULL, needed_size + META_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	DIE(request == ((void *) -1), "Error at mmap allocation\n");

	struct block_meta *block = (struct block_meta *)request;

	block->status = STATUS_MAPPED;
	block->size = needed_size;
	block->next = NULL;
	block->prev = NULL;

	return block;
}

void *os_malloc(size_t size)
{
	//This helps me in preallocate to know that the max size is MMAP_THRESHOLD
	int is_malloc = 1;

	if (size <= 0)
		return NULL;

	struct block_meta *block = NULL;

	//If the size is bigger than MMAP_THRESHOLD I allocate it with mmap, if
	//it is smaller, I go to brk_alloc
	if (ALIGN(size) + META_SIZE >= MMAP_THRESHOLD) {
		block = mmap_alloc(ALIGN(size));

		if (mmap_blocks == NULL) {
			mmap_blocks = block;

		} else {
			struct block_meta *last_mmap_block = find_last_block(mmap_blocks);
 			block->prev = last_mmap_block;
			last_mmap_block->next = block;
		}

	} else {
		block = brk_alloc(ALIGN(size), is_malloc);

		if (brk_blocks == NULL)
			brk_blocks = block;
	}

	return (void *)(((char *)block) + META_SIZE);
}

void remove_block(struct block_meta *block)
{
	if (block == mmap_blocks) {
		mmap_blocks = block->next;

	} else {
		block->prev->next = block->next;

		if (block->next)
			block->next->prev = block->prev;
	}
}

//In this function I free the allocated memory. If it is allocated with
//sbrk I just set its status to free so I can reuse that memory. If it
//is allocated with mmap, this is memory that cannot be reused so I
//use munmap and I remove the block from the list
void os_free(void *ptr)
{
	if (ptr == NULL)
		return;

	struct block_meta *block = (struct block_meta *) (((char *) ptr) - META_SIZE);

	if (block->status == STATUS_ALLOC) {
		block->status = STATUS_FREE;

	} else {
		if (block->status == STATUS_MAPPED) {
			remove_block(block);

			int ret = munmap(block, block->size + META_SIZE);

			DIE(ret == -1, "Error at munmap in free\n");
		}
	}
}

//The calloc function is similar to malloc. The differences are that I need
//to use memset to set the memory to 0 and I compare the size with PAGE_SIZE
//instead of MMAP_THRESHOLD
void *os_calloc(size_t nmemb, size_t size)
{
	int is_malloc = 0;

	if (nmemb <= 0 || size <= 0)
		return NULL;

	struct block_meta *block = NULL;

	if (ALIGN(size * nmemb) + META_SIZE >= (size_t)PAGE_SIZE) {
		block = mmap_alloc(ALIGN(size * nmemb));

		if (mmap_blocks == NULL) {
			mmap_blocks = block;

		} else {
			struct block_meta *last_mmap_block = find_last_block(mmap_blocks);

			block->prev = last_mmap_block;
			last_mmap_block->next = block;
		}

	} else {
		block = brk_alloc(ALIGN(size * nmemb), is_malloc);

		if (brk_blocks == NULL)
			brk_blocks = block;
	}

	void *ptr = (void *)(((char *)block) + META_SIZE);

	memset(ptr, 0, size * nmemb);

	return ptr;
}

//In this function I reallocate the size of a given pointer
void *os_realloc(void *ptr, size_t size)
{
	//If ptr is null it means that I need to allocate the memory
	if (ptr == NULL)
		return os_malloc(size);

	//If size is 0 it means that I need to free the memory
	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	struct block_meta *block = (struct block_meta *) (((char *) ptr) - META_SIZE);

	if (block->status == STATUS_FREE)
		return NULL;

	//If the size is the same as the ptr's size it stays the same
	if (block->size == ALIGN(size))
		return ptr;

	//Memory allocated with mmap cannot be used so if I need to use mmap to reallocate
	//or the memory was allocated with mmap I have to allocate a new block, copy the
	//data from the given block in the new one and free ptr
	if (ALIGN(size) + META_SIZE >= MMAP_THRESHOLD || block->status == STATUS_MAPPED) {
		void *new_ptr = os_malloc(size);

		memcpy(new_ptr, ptr, MIN(block->size, ALIGN(size)));
		os_free(ptr);

		return new_ptr;
	}

	//If the memory was allocated with sbrk I check if the size to be reallocated
	//is bigger than the initial size. If it is, I first check if the block is
	//the last one in the list in order to expand it. If it is not, I coalesce
	//the blocks starting with my block if the next ones are free until I have
	//a size bigger than the given size. If I still need space, I allocate a
	//new memory block and free the old one
	if (ALIGN(size) + META_SIZE < MMAP_THRESHOLD) {
		if (ALIGN(size) > block->size) {
			struct block_meta *last_block = find_last_block(brk_blocks);

			if (block->next == NULL) {
				void *ptr1 = (void *)sbrk(ALIGN(size) - last_block->size);

				DIE(ptr1 == (void *) -1, "Eroor at sbrk in heap preallocation\n");

				last_block->status = STATUS_ALLOC;
				last_block->size = ALIGN(size);
			}

			while (ALIGN(size) > block->size && block->next && block->next->status == STATUS_FREE) {
				block->size += block->next->size + META_SIZE;

				struct block_meta *elim_block = block->next;

				if (elim_block->next) {
					elim_block->next->prev = block;
					block->next = elim_block->next;
				} else {
					block->next = NULL;
				}
			}

			if (ALIGN(size) > block->size) {
				void *new_ptr = os_malloc(size);

				memcpy(new_ptr, ptr, MIN(block->size, ALIGN(size)));
				os_free(ptr);

				return new_ptr;
			}
		}

		//If the size is smaller than the one that needs to be reallocated
		//I split the block to not use more memory than I need
		if (ALIGN(size) + META_SIZE + ALIGN(1) <= block->size)
			block = split_blocks(block, ALIGN(size));
	}

	return (void *)(((char *)block) + META_SIZE);
}
