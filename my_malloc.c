/*
 * CS 2110 Spring 2018
 * Author: Nick Liccini
 */

/* we need this for uintptr_t */
#include <stdint.h>
/* we need this for memcpy/memset */
#include <string.h>
/* we need this to print out stuff*/
#include <stdio.h>
/* we need this to use boolean types */
#include <stdbool.h>
/* we need this for my_sbrk */
#include "my_sbrk.h"
/* we need this for the metadata_t struct and my_malloc_err enum definitions */
#include "my_malloc.h"

/* Our freelist structure - our freelist is represented as two doubly linked lists
 * the address_list orders the free blocks in ascending address
 * the size_list orders the free blocks by size
 */

metadata_t *address_list;
metadata_t *size_list;

/* Set on every invocation of my_malloc()/my_free()/my_realloc()/
 * my_calloc() to indicate success or the type of failure. See
 * the definition of the my_malloc_err enum in my_malloc.h for details.
 * Similar to errno(3).
 */
enum my_malloc_err my_malloc_errno;

static void sort_size_list(metadata_t* block) {
	// "Remove" the block from the size_list to edit it
	if (block == size_list) {
		if (block->next_size) {
			block->next_size->prev_size = NULL;
		}
		size_list = block->next_size;
	} else {
		if (block->prev_size) {
			block->prev_size->next_size = block->next_size;
		}
		if (block->next_size) {
			block->next_size->prev_size = block->prev_size;
		}
	}
	block->next_size = NULL;
	block->prev_size = NULL;

	// If the list is empty now, then just add the block
	if (size_list == NULL) {
		size_list = block;
		return;
	}

	metadata_t* curr_size_node = size_list;
	bool at_start = true;
	bool at_end = (curr_size_node->next_size == NULL);
	
	// Find the location to insert the block
	while (block->size > curr_size_node->size) {
		at_start = false;
		if (curr_size_node->next_size == NULL) {
			at_end = true;
			break;
		}
		curr_size_node = curr_size_node->next_size;
	}

	// Insert the block in the appropriate place
	if (block->size < size_list->size) {
		// Insert as the front node
		block->next_size = size_list;
		size_list->prev_size = block;
		size_list = block;
	} else if (at_end) {
		// Insert as the last node
		block->prev_size = curr_size_node;
		block->next_size = NULL;
		curr_size_node->next_size = block;
	} else {
		// Insert before curr_size_node
		if (curr_size_node->prev_size) {
			curr_size_node->prev_size->next_size = block;
		}
		block->prev_size = curr_size_node->prev_size;
		block->next_size = curr_size_node;
		curr_size_node->prev_size = block;
	}

	// If the block is the new smallest node, update the size_list head
	if (at_start) {
		size_list = block;
	}
}

static void sort_addr_list(metadata_t* block) {
	// "Remove" the block from the address_list to edit it
	if (address_list == block) {
		return;
	}

	if (block->prev_addr) {
		block->prev_addr->next_addr = block->next_addr;
	}
	if (block->next_addr) {
		block->next_addr->prev_addr = block->prev_addr;
	}
	block->next_addr = NULL;
	block->prev_addr = NULL;

	metadata_t* curr_addr_node = address_list;
	bool at_start = true;
	bool at_end = (curr_addr_node->next_addr == NULL);
	
	// Find the location to insert the block
	while (block > curr_addr_node) {
		at_start = false;
		if (curr_addr_node->next_addr == NULL) {
			at_end = true;
			break;
		}
		curr_addr_node = curr_addr_node->next_addr;
	}

	// Insert the block in the approprieate place
	if (at_start && at_end) {
		// There is only one node
		address_list = block;
	} else if (at_end) {
		// Insert as the last node
		block->prev_addr = curr_addr_node;
		block->next_addr = NULL;
		curr_addr_node->next_addr = block;
	} else {
		// Insert before curr_addr_node
		if (curr_addr_node->prev_addr) {
			curr_addr_node->prev_addr->next_addr = block;
		}
		block->prev_addr = curr_addr_node->prev_addr;
		block->next_addr = curr_addr_node;
		curr_addr_node->prev_addr = block;
	}

	// If the block is the new first node, update the address_list head
	if (at_start) {
		address_list = block;
	}
}

static void prepare_block_to_return(metadata_t* block) {
	// Update the metadata
	unsigned long canary = ((uintptr_t) block ^ CANARY_MAGIC_NUMBER) + 1;
	block->canary = canary;
	block->next_addr = NULL;
	block->prev_addr = NULL;
	block->next_size = NULL;
	block->prev_size = NULL;
	unsigned long* trailing_canary = (unsigned long *) ((uintptr_t) block + block->size - sizeof(unsigned long));
	*trailing_canary = canary;
}

static void merge_node_to_free_left(metadata_t* node_to_free, metadata_t* left_node) {
	// Combine node_to_free and left_node 
	left_node->size += node_to_free->size;

	// Update the canaries of the new block
	int canary = ((uintptr_t) left_node ^ CANARY_MAGIC_NUMBER) + 1;
	left_node->canary = canary;
	unsigned long* trailing_canary = (unsigned long *) ((uintptr_t) left_node + left_node->size - sizeof(unsigned long));
	*trailing_canary = canary;

	// Remove left_node from size_list
	if (left_node == size_list) {
		if (left_node->next_size) {
			left_node->next_size->prev_size = NULL;
		}
		size_list = left_node->next_size;
	} else {
		if (left_node->prev_size) {
			left_node->prev_size->next_size = left_node->next_size;
		}
		if (left_node->next_size) {
			left_node->next_size->prev_size = left_node->prev_size;
		}
	}
	left_node->prev_size = NULL;
	left_node->next_size = NULL;

	// Insert node_to_free into size_list
	sort_size_list(left_node);

	// Remove left_node from address_list
	if (left_node == address_list) {
		if (left_node->next_addr) {
			left_node->next_addr->prev_addr = NULL;
		}
		address_list = left_node->next_addr;
	} else {
		if (left_node->prev_addr) {
			left_node->prev_addr->next_addr = left_node->next_addr;
		}
		if (left_node->next_addr) {
			left_node->next_addr->prev_addr = left_node->prev_addr;
		}
	}
	left_node->prev_addr = NULL;
	left_node->next_addr = NULL;

	// Insert the node_to_free in the address_list
	sort_addr_list(left_node);
}

static void merge_node_to_free_right(metadata_t* node_to_free, metadata_t* right_node) {
	// Combine node_to_free and right_node 
	node_to_free->size += right_node->size;

	// Update the canaries of the new block
	int canary = ((uintptr_t) node_to_free ^ CANARY_MAGIC_NUMBER) + 1;
	node_to_free->canary = canary;
	unsigned long* trailing_canary = (unsigned long *) ((uintptr_t) node_to_free + node_to_free->size - sizeof(unsigned long));
	*trailing_canary = canary;

	// Remove right_node from size_list
	if (right_node == size_list) {
		if (right_node->next_size) {
			right_node->next_size->prev_size = NULL;
		}
		size_list = right_node->next_size;
	} else {
		if (right_node->prev_size) {
			right_node->prev_size->next_size = right_node->next_size;
		}
		if (right_node->next_size) {
			right_node->next_size->prev_size = right_node->prev_size;
		}
	}
	right_node->prev_size = NULL;
	right_node->next_size = NULL;

	// Insert node_to_free into size_list
	sort_size_list(node_to_free);

	// Remove right_node from address_list
	if (right_node == address_list) {
		if (right_node->next_addr) {
			right_node->next_addr->prev_addr = NULL;
		}
		address_list = right_node->next_addr;
	} else {
		if (right_node->prev_addr) {
			right_node->prev_addr->next_addr = right_node->next_addr;
		}
		if (right_node->next_addr) {
			right_node->next_addr->prev_addr = right_node->prev_addr;
		}
	}
	right_node->prev_addr = NULL;
	right_node->next_addr = NULL;

	// Insert the node_to_free in the address_list
	sort_addr_list(node_to_free);
}

/* MALLOC
 * See my_malloc.h for documentation
 */
void *my_malloc(size_t size) {
	my_malloc_errno = NO_ERROR;

	if (size == 0) {
		return NULL;
	}

	// Calculate actual block size
	size_t block_size = TOTAL_METADATA_SIZE + size;
	if (block_size > SBRK_SIZE) {
		my_malloc_errno = SINGLE_REQUEST_TOO_LARGE;
		return NULL;
	}

	unsigned long canary;
	unsigned long* trailing_canary;
	void* block_body_addr;
	bool block_found = false;

	// If this is the first call to my_malloc, get memory from my_sbrk
	if (size_list == NULL || address_list == NULL) {
		size_list = my_sbrk(SBRK_SIZE);
		if (!size_list) {
			my_malloc_errno = OUT_OF_MEMORY;
			return NULL;
		}

		address_list = size_list;

		size_list->size = SBRK_SIZE;
		size_list->next_addr = NULL;
		size_list->prev_addr = NULL;
		size_list->next_size = NULL;
		size_list->prev_size = NULL;
		canary = ((uintptr_t) size_list ^ CANARY_MAGIC_NUMBER) + 1;
		size_list->canary = canary;
		trailing_canary = (unsigned long *) ((uintptr_t) size_list + size_list->size - sizeof(unsigned long));
		*trailing_canary = canary;
	}

	metadata_t* curr_size_node = size_list;
	
	// Traverse the lists looking for a fitting block
	while (!block_found && curr_size_node != NULL) {
		if (curr_size_node->size == block_size) {
			// Remove the block from size_list and address_list
			if (curr_size_node->prev_addr) {
				curr_size_node->prev_addr->next_addr = curr_size_node->next_addr;
			}
			if (curr_size_node->next_addr) {
				curr_size_node->next_addr->prev_addr = curr_size_node->prev_addr;
			}
			if (curr_size_node->prev_size) {
				curr_size_node->prev_size->next_size = curr_size_node->next_size;
			}
			if (curr_size_node->next_size) {
				curr_size_node->next_size->prev_size = curr_size_node->prev_size;
			}

			// Update the freelist pointers
			if (curr_size_node == size_list) {
				size_list = curr_size_node->next_size;
			}
			if (curr_size_node == address_list) {
				address_list = curr_size_node->next_addr;
			}

			prepare_block_to_return(curr_size_node);

			// Set the return address
			block_body_addr = (void*) (curr_size_node + 1);

			block_found = true;
		} else if (curr_size_node->size >= block_size + MIN_BLOCK_SIZE) {
			// Split the back of the block
			size_t new_node_size = curr_size_node->size - block_size;
			curr_size_node->size = new_node_size;

			// Set the canaries of the updated curr_size_addr
			canary = ((uintptr_t) curr_size_node ^ CANARY_MAGIC_NUMBER) + 1;
			curr_size_node->canary = canary;
			trailing_canary = (unsigned long *) ((uintptr_t) curr_size_node + curr_size_node->size - sizeof(unsigned long));
			*trailing_canary = canary;

			// Get the address of the pointer to return
			block_body_addr = (void*) ((uintptr_t) curr_size_node + (uintptr_t) new_node_size + sizeof(metadata_t));

			// Use that address to create a metadata block (not included in the freelist)
			metadata_t* block_start_addr = (metadata_t*) block_body_addr - 1;
			block_start_addr->size = block_size;
			prepare_block_to_return(block_start_addr);

			block_found = true;

			sort_size_list(curr_size_node);
		}

		// Break early if the last node is found
		if (curr_size_node->next_size == NULL) {
			break;
		}

		// Get the next block in the freelist
		curr_size_node = curr_size_node->next_size;
	}
	
	// Call my_sbrk if no block fits
	if (!block_found) {
		// Get more memory
		metadata_t* new_heap_addr = my_sbrk(SBRK_SIZE);
		if (!new_heap_addr) {
			my_malloc_errno = OUT_OF_MEMORY;
			return NULL;
		}

		// Get the last node in the address_list
		metadata_t* curr_addr_node = address_list;
		while (curr_addr_node->next_addr != NULL) {
			curr_addr_node = curr_addr_node->next_addr;
		}

		// Check if the new block can be merged with the last node in the address_list
		if (new_heap_addr == (metadata_t*) ((uint8_t*) curr_addr_node + curr_addr_node->size)) {
			curr_addr_node->size += SBRK_SIZE;
			canary = ((uintptr_t) curr_addr_node ^ CANARY_MAGIC_NUMBER) + 1;
			curr_addr_node->canary = canary;
			trailing_canary = (unsigned long *) ((uintptr_t) curr_addr_node + curr_addr_node->size - sizeof(unsigned long));
			*trailing_canary = canary;

			sort_size_list(curr_addr_node);
			sort_addr_list(curr_addr_node);
		} else {
			// Create a new block
			new_heap_addr->size = SBRK_SIZE;
			new_heap_addr->next_addr = NULL;
			new_heap_addr->prev_addr = NULL;
			new_heap_addr->next_size = NULL;
			new_heap_addr->prev_size = NULL;

			// Update the canaries of the new block
			canary = ((uintptr_t) new_heap_addr ^ CANARY_MAGIC_NUMBER) + 1;
			new_heap_addr->canary = canary;
			trailing_canary = (unsigned long *) ((uintptr_t) new_heap_addr + new_heap_addr->size - sizeof(unsigned long));
			*trailing_canary = canary;

			sort_size_list(new_heap_addr);
			sort_addr_list(new_heap_addr);
		}

		// Try to allocate the block now
		block_body_addr = my_malloc(size);
	}

    return block_body_addr;
}

/* REALLOC
 * See my_malloc.h for documentation
 */
void *my_realloc(void *ptr, size_t size) {
	my_malloc_errno = NO_ERROR;

	// If ptr == NULL, treat realloc as malloc
	if (ptr == NULL) {
		void* new_ptr = my_malloc(size);
		return new_ptr;
	}

	// If size == 0, trat realloc as free
	if (size == 0) {
		my_free(ptr);
		return NULL;
	}

	// Find the address of the block start
	metadata_t* metadata_ptr = (metadata_t*) ptr - 1;

	// Check for corrupted canaies
	unsigned long expected_canary = ((uintptr_t) metadata_ptr ^ CANARY_MAGIC_NUMBER) + 1;
	unsigned long* trailing_canary = (unsigned long *)((uintptr_t) metadata_ptr + metadata_ptr->size - sizeof(unsigned long));
	if (metadata_ptr->canary != expected_canary) {
		my_malloc_errno = CANARY_CORRUPTED;
		return NULL;
	}
	if (*trailing_canary != expected_canary) {
		my_malloc_errno = CANARY_CORRUPTED;
		return NULL;
	}

	// Create a new pointer with the input size
	void* new_ptr = my_malloc(size);
	if (!new_ptr) {
		return NULL;
	}

	// Check if whether to copy shrunken memory or full memory
	if (metadata_ptr->size > size) {
		memcpy(new_ptr, ptr, size);
	} else {
		memcpy(new_ptr, ptr, metadata_ptr->size - TOTAL_METADATA_SIZE);
	}

	// Free the old pointer and return the new one
	my_free(ptr);

    return new_ptr;
}

/* CALLOC
 * See my_malloc.h for documentation
 */
void *my_calloc(size_t nmemb, size_t size) {
	my_malloc_errno = NO_ERROR;

	// Call my_malloc for nmemb elements of size size
	void* ptr = my_malloc(nmemb * size);
	if (!ptr) {
		return NULL;
	}

	// Zero out the entire block
	memset(ptr, 0, nmemb * size);

	// Return the pointer
	return ptr;
}

/* FREE
 * See my_malloc.h for documentation
 */
void my_free(void *ptr) {
	my_malloc_errno = NO_ERROR;
	metadata_t* node_to_free = (metadata_t*) ptr;

	if (ptr == NULL) {
		return;
	}

	// Calculate actual block address
	node_to_free = node_to_free - 1;

	// Check for corrupted canaies
	unsigned long expected_canary = ((uintptr_t) node_to_free ^ CANARY_MAGIC_NUMBER) + 1;
	unsigned long* trailing_canary = (unsigned long *)((uintptr_t) node_to_free + node_to_free->size - sizeof(unsigned long));
	if (node_to_free->canary != expected_canary) {
		my_malloc_errno = CANARY_CORRUPTED;
		return;
	}
	if (*trailing_canary != expected_canary) {
		my_malloc_errno = CANARY_CORRUPTED;
		return;
	}

	// If the freelist is empty, simply add the node and be done
	if (address_list == NULL) {
		address_list = node_to_free;
		size_list = node_to_free;
		return;
	}
	
	// Traverse through the address_list to find the closest block in memory
	bool at_end = address_list->next_addr == NULL;
	metadata_t* curr_addr_node = address_list;
	while (node_to_free > curr_addr_node) {
		if (curr_addr_node->next_addr == NULL) {
			at_end = true;
			break;
		}
		curr_addr_node = curr_addr_node->next_addr;
	}

	// curr_addr_node is now the node just after node_to_free in memory
	metadata_t* left_node = curr_addr_node;
	if (!at_end) {
		left_node = curr_addr_node->prev_addr;
	}
	metadata_t* right_node = curr_addr_node;

	bool block_merged = false;

	// Merge left the freed block
	if (left_node) {
		if (node_to_free == (metadata_t*) ((uint8_t*) left_node + left_node->size)) {
			merge_node_to_free_left(node_to_free, left_node);
			node_to_free = left_node;
			block_merged = true;
		}
	}

	// Merge right the freed block
	if (right_node) {
		if (right_node == (metadata_t*) ((uint8_t*) node_to_free + node_to_free->size)) {
			merge_node_to_free_right(node_to_free, right_node);
			block_merged = true;
		}
	}

	// Simply add the block to the freelist if it cannot be merged
	if (!block_merged) {
		sort_addr_list(node_to_free);
		sort_size_list(node_to_free);		
	}
}