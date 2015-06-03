#include "page.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include <stdio.h>
#include "vm/swap.h"
#include "threads/synch.h"

static struct lock lock;

/* mapping the virtual address to the physical address */
static bool install_page (struct thread* t, void *upage, void *kpage, bool writable);
static void free_vm_entry(struct vm_entry* vme);

/* hash function (wrapped) */
static unsigned vm_hash_func (const struct hash_elem* e, void* aux UNUSED)
{
  struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);
  //printf("vm_hash_func : %p, %p\n", vme->vaddr, pg_round_down (vme->vaddr));

  void* vaddr = pg_round_down (vme->vaddr);

  unsigned hash = hash_bytes (&vaddr, sizeof vaddr);
  //printf("hash : %u\n", hash);
  return hash;
}

/* hash compare function */
static bool vm_less_func (const struct hash_elem* a, const struct hash_elem* b, void* aux UNUSED)
{
  struct vm_entry *vme_a, *vme_b;
  
  vme_a = hash_entry (a, struct vm_entry, elem);
  vme_b = hash_entry (b, struct vm_entry, elem);

  if (pg_round_down(vme_a->vaddr) < pg_round_down(vme_b->vaddr))
    return true;
  else
    return false;
}

//vm's desturoctor function. Don't need to delete pages.
static void vm_destroy_func (struct hash_elem* e, void* aux UNUSED)
{
  struct thread* t = thread_current();
  struct vm_entry* vme = hash_entry (e, struct vm_entry, elem);

  free_vm_entry(vme);
}

void page_init(){
  lock_init(&lock);
}

/* initialize the vm hash table by above functions */
void vm_init (struct hash* vm)
{
  //printf("vm_init %p\n", (void*)vm);
  hash_init (vm, vm_hash_func, vm_less_func, NULL);
}

void vm_destroy (struct hash* vm)
{
  hash_destroy (vm, vm_destroy_func);
}

/*find vm_entry by virtual address  */
struct vm_entry* find_vme (void* vaddr)
{
  struct hash_elem* e = NULL;

  /* temp vm_entry (saving addr to find element) */
  struct vm_entry p;
  p.vaddr = vaddr;

  //printf("find_vme vm(%p)\n", &thread_current()->vm);

  /* first argument: current thread's vm hash table */
  e = hash_find (&thread_current()->vm, &p.elem);

  if (e != NULL)
    return hash_entry (e, struct vm_entry, elem);
  else
    return NULL;
}

//insert allocated vm_entry instance to thread vm hash
bool insert_vme (struct hash* vm, struct vm_entry* vme)
{

   lock_acquire(&lock);

  //printf("insert_vme (%p) : %p\n", (void*)vm, (void*)vme->vaddr);
  if (hash_insert (vm, &vme->elem) != NULL){
    lock_release(&lock);
    return true;
  }
  else{
    lock_release(&lock);
    return false;
  }
}

//remove vm_entry instance to thread vm hash
bool delete_vme (struct hash* vm, struct vm_entry* vme)
{
  lock_acquire(&lock);
  if (hash_delete (vm, &vme->elem) != NULL){
    free_vm_entry(vme);
    lock_release(&lock);
    return true;
  }
  else{
    lock_release(&lock);
    return false;
  }
}

/* if vaddr exist in vm table, pinned = false */
void unpin_ptr (void* vaddr)
{
  struct vm_entry* vme = find_vme (vaddr);
  if (vme)
    vme->pinned = false;
}

void unpin_string (void* str)
{
  unsigned i;
  for (i = 0; i < strlen (str); ++i)
    unpin_ptr ((char*)str + i);
}

void unpin_buffer (void* buffer, unsigned size)
{
  unsigned i;
  for (i = 0; i < size; ++i)
    unpin_ptr ((char*)buffer + i);
}

/* load file (address kaddr ~ kaddr+read_bytes) 
   and kaddr+read_bytes ~ kaddr+read_bytes+zero_bytes = 0*/
bool load_file (void* kaddr, struct vm_entry *vme)
{
  off_t read;
  ASSERT(vme->file);
  read = file_read_at (vme->file, kaddr, vme->read_bytes, vme->offset); 
  memset (kaddr + vme->read_bytes, 0, vme->zero_bytes);
  return true;
}

struct vm_entry* alloc_vmentry(uint8_t type, void* vaddr){
  struct thread* t = thread_current();
  struct vm_entry* vme;

  lock_acquire(&t->lock_vme);

  vme = (struct vm_entry*)malloc(sizeof(struct vm_entry));
  if(vme == NULL){
    lock_release(&t->lock_vme);
    return NULL;
  }
  

  memset(vme, 0, sizeof(struct vm_entry));
  vme->type = type;
  vme->vaddr = vaddr;
  vme->mfile_id = -1;
  vme->swap_slot = SWAP_ERROR;

  insert_vme(&t->vm, vme);
  lock_release(&t->lock_vme);
  return vme;
}

//all page is allocated by this function
struct page* alloc_page(enum palloc_flags flags){
  struct page* page = NULL;
  void* kaddr = NULL;

  lock_acquire(&lock);
  //get physical memory
  kaddr = palloc_get_page(flags);
  
  //if cannot alloc physical memory
  if(kaddr == NULL){ 
    lock_release(&lock);
    try_to_free_pages(flags); //release least recently used page
    lock_acquire(&lock);
    kaddr = palloc_get_page(flags); //try alloc physical memory again!
  }

  if(kaddr){
    page = (struct page*)malloc(sizeof(struct page));   //create page instance
    memset(page, 0, sizeof(struct page));
    page->kaddr = kaddr;
    page->thread = thread_current();
    add_page_to_lru_list(page);     //add to LRU list
  }  
  lock_release(&lock);

  return page;
}

bool page_set_vmentry(struct page* page, struct vm_entry* vme){
  //printf("page_set_vmentry | vaddr(%p), kaddr(%p)\n", vme->vaddr, page->kaddr);
  lock_acquire(&lock);

  //install vaddr - kaddr mapping to page directory
  if(install_page (page->thread, vme->vaddr, page->kaddr, vme->writable)){ 
    //if succeeded, setup vme to page, page to vme either.
    page->vme = vme;

    vme->is_loaded = page->kaddr != NULL;
    vme->page = page;
    lock_release(&lock);
    return true;
  }
  else{
    lock_release(&lock);
    return false;
  }
}

void free_vm_entry(struct vm_entry* vme){
  if(!vme)
    return;

  if(vme->page){
    ASSERT(vme->page->vme == vme);
    free_page(vme->page, false);
    ASSERT(vme->page == NULL);
  }

  


  //hash_delete(&t->vm, e);
  free(vme);
}

void free_page(struct page* page, bool preserve){
    if(!page)
      return;

    lock_acquire(&lock);

    //swap out memories
    if(page->vme){
      if(preserve){
        switch(page->vme->type){
          case VM_BIN:

            //if it is dirtied, it's type changed to VM_ANON, and swapped out
            if(pagedir_is_dirty(page->thread->pagedir, page->vme->vaddr)){
              ASSERT(page->vme->writable);
              page->vme->swap_slot = swap_out(page->kaddr);
              page->vme->type = VM_ANON;
              page->vme->file = NULL;
            }
            break;
          case VM_FILE:
            if(pagedir_is_dirty(page->thread->pagedir, page->vme->vaddr)){
              //flush memory mapped file
              mmap_vmentry_flush(page->thread, page->vme);
            }
            break;
          case VM_ANON:
            page->vme->swap_slot = swap_out(page->kaddr);
            break;
        }
      }
      else{
        if(page->vme->swap_slot != SWAP_ERROR)
          release_swap_slot(page->vme->swap_slot);
        page->vme->swap_slot = SWAP_ERROR;
      }

      page->vme->is_loaded = false;
      //printf("page(%p), vme(%p), thread(%p), kaddr(%p), vaddr(%p)\n", page, page->vme, page->thread, page->kaddr, page->vme->vaddr);
      ASSERT(page->kaddr == pagedir_get_page(page->thread->pagedir, page->vme->vaddr));
      pagedir_clear_page(page->thread->pagedir, page->vme->vaddr); //remove from thread page directory
      page->vme->page = NULL;
      
    }

    //delete from LRU list
    del_page_from_lru_list(page);   
    ASSERT(page->kaddr != NULL);
    palloc_free_page (page->kaddr); //free physical page

    free(page);

    lock_release(&lock);
}

static bool
install_page (struct thread* t, void *upage, void *kpage, bool writable)
{
  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}