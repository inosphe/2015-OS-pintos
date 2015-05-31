#ifndef FRAME_H
#define FRAME_H

#include "vm/page.h"


void lru_list_init(void);

/*
	add page to lru list
*/
void add_page_to_lru_list(struct page* page);

/*
	delete page from lru list
*/
void del_page_from_lru_list(struct page* page);


//free physical meories - swap out pages using `clock` algorithm
void try_to_free_pages(enum palloc_flags flags);

#endif