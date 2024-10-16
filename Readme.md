
Memory Allocator - osmem

Overview
This repository contains a custom memory allocator implementation in C, which is designed to manage memory allocation using both sbrk and mmap. It mimics the behavior of standard memory allocation functions (malloc, calloc, realloc, and free), with added mechanisms for optimizing memory usage by preallocating space and reusing free blocks.

Key Features:
Dynamic Memory Allocation: Allocates memory using sbrk and mmap depending on the size of the request.
Block Splitting: Splits larger blocks into smaller ones to better fit memory requests.
Coalescing: Combines adjacent free blocks to avoid fragmentation and reuse memory efficiently.
Heap Preallocation: Preallocates large blocks of memory to reduce the number of system calls, optimizing performance.
Support for malloc, calloc, realloc, and free: Provides functionality equivalent to standard memory management functions.
Files
osmem.c
Core Memory Management Functions:

os_malloc(size_t size): Allocates memory of the given size. If the requested size is larger than a threshold, it uses mmap; otherwise, it uses sbrk.
os_free(void *ptr): Frees the allocated memory. If the memory was allocated via mmap, it is unmapped; if allocated via sbrk, its status is marked as free for potential reuse.
os_calloc(size_t nmemb, size_t size): Allocates memory for an array of nmemb elements, each of the given size, and initializes the allocated memory to zero.
os_realloc(void *ptr, size_t size): Reallocates memory to change the size of the memory block pointed to by ptr.
Helper Functions:

find_last_block(): Finds the last block in a linked list of memory blocks.
split_blocks(): Splits a large block into smaller blocks, ensuring efficient memory usage.
find_best_block(): Finds the best-fit free block that can accommodate a given size.
coalesce_blocks(): Combines adjacent free blocks to reduce fragmentation.
heap_preallocation(): Preallocates memory using sbrk to minimize the number of system calls.
block_meta.h
Block Metadata: Defines the structure for metadata associated with each block of memory. Each block has information such as its size, status (allocated or free), and pointers to the next/previous blocks.
How It Works
Memory Allocation
When allocating memory with os_malloc:

If the requested size is greater than a predefined threshold (128 KB by default), the function allocates memory using mmap.
For smaller requests, it allocates memory using sbrk. If it's the first time using sbrk, it preallocates a large chunk of memory.
The allocator attempts to reuse free memory blocks when possible by coalescing adjacent free blocks and splitting larger blocks to fit smaller requests.
Memory Deallocation
The os_free function frees memory blocks. For memory allocated with sbrk, it marks the block as free for future reuse. For memory allocated with mmap, it unmaps the memory from the system.

Reallocation and Zero-Initialization
The os_realloc function resizes an existing memory block. If more space is required, it may coalesce adjacent free blocks or allocate a new block.
The os_calloc function allocates zero-initialized memory for arrays.