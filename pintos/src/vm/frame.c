#include "frame.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "vm/swap.h"

static struct list 			lru_list;
static struct lock			lru_lock;

static struct list_elem* gt_next_lru_clock();

//initialize lru list, lock object
void lru_list_init(void){
	list_init(&lru_list);
	lock_init(&lru_lock);
}

//add page to lru list
void add_page_to_lru_list(struct page* page){
	lock_acquire(&lru_lock);
	list_push_back(&lru_list, &page->lru);
	lock_release(&lru_lock);
}


//delete page from lru list
void del_page_from_lru_list(struct page* page){
	lock_acquire(&lru_lock);
	list_remove(&page->lru);
	lock_release(&lru_lock);
}


//FIFO
struct list_elem* gt_next_lru_clock(){
	struct list_elem* 	e;

	lock_acquire(&lru_lock);

	e = list_begin(&lru_list);
	while(true)
	{
		struct page* page = list_entry(e, struct page, lru);
		struct thread* thread = page->thread;

		if(!page->vme->pinned){	//pinned page is not freed.

			//if page's acceess bit is 1, it will have second chance
			if(pagedir_is_accessed(thread->pagedir, page->vme->vaddr)){
				pagedir_set_accessed(thread->pagedir, page->vme->vaddr, false);
			}
			else{

				lock_release(&lru_lock);

				//else it's chosen to be victim;
				return page;
			}	
		}
		

		//when meet the end of list, begin again
		e = list_next(e);
		if(e == list_end(&lru_list)){
			e = list_begin(&lru_list);
		}
	}

	lock_release(&lru_lock);
	
	return NULL;
}


void try_to_free_pages(enum palloc_flags flags){
	struct page* victim = gt_next_lru_clock();
	if(victim == NULL){
		printf("no victim\n");
		return;
	}

	//free chosen victim;
	free_page(victim);  
}