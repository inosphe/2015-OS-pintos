#ifndef SWAP_H
#define SWAP_H

#define SWAP_ERROR SIZE_MAX

#include "stddef.h"

/*
	initialize swap area

*/
void swap_init();

/*
	load swap file to physical memory
	swapslot[used_index] => kaddr
*/
void swap_in(size_t used_index, void* kaddr);
void release_swap_slot(size_t used_index);

/*
	save physical memory to swap file;
	kaddr => swapslot[index]
	return: index
*/

size_t swap_out(void* kaddr);

#endif
