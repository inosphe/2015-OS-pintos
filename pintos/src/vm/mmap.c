#include "vm/mmap.h"
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include <list.h>
#include <assert.h>
#include "vm/swap.h"


static void vm_destroy_func (struct hash_elem* e, void* aux);
static void do_mummap(struct mmap_file* mfile);

mapid_t mmap (int fd, void *addr)
{
  static int id = 0;
  struct file* file = process_get_file(fd);
  struct thread* t = thread_current();
  struct mmap_file* mfile;
  int success = 0;
  off_t ofs = 0;
  uint32_t read_bytes=0;

  if(!file){
    return -1;
  }

  //source file descriptions may be closed. so reopen the file
  file = file_reopen(file);
  if(!file){
    return -1;
  }

  //if invalid addr, fail
  if ((uint32_t)addr < 0x8048000 || (uint32_t)addr >= 0xc0000000)
    return -1;

  //if not aligned, fail
  if((uint32_t)addr & PGMASK){
    return -1;
  }

  mfile = (struct mmap_file*)malloc(sizeof(struct mmap_file));
  mfile->mapid = ++id;
  mfile->file = file;
  read_bytes = file_length(file);

  vm_init(&mfile->vm);
  mfile->vm.aux = mfile;
  success = 1;

  //make vm_entries per 4K (file size)
  while(read_bytes>0){
      struct vm_entry* vme, *vme_check;

      //segment violation
      if(addr){
        vme_check = find_vme(addr);
        if(vme_check){
          success = 0;
          break;
        }
      }

      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      //source of memory mapped file is FILE
      vme = alloc_vmentry(VM_FILE, addr);
      vme->is_loaded = false;
      vme->pinned = false;
      vme->file = file;
      vme->offset = ofs;
      vme->read_bytes = page_read_bytes;
      vme->zero_bytes = page_zero_bytes;
      vme->writable = file_write_allowed(file);
      vme->mfile_id = mfile->mapid;

      //insert to both thread vm, mmfile vm
      hash_insert (&mfile->vm, &vme->mmap_elem);
      insert_vme (&t->vm, vme);

      ofs += page_read_bytes;   //next offset
      addr += PGSIZE;           //next virtual address

      read_bytes -= page_read_bytes;
  }

  if(success){
    list_push_back(&t->list_mmap, &mfile->elem);
    return mfile->mapid;
  }
  else{
    do_mummap(mfile);
    return MAP_FAILED;
  }
}

/* unmap / release every vm_entries, close file, etc... */
static void do_mummap(struct mmap_file* mfile){
    hash_destroy (&mfile->vm, vm_destroy_func); 
    file_close(mfile->file);
    
}

/* release mmap by mapid */
void munmap (mapid_t id)
{
  struct mmap_file* mfile = get_mmap_file(id);
  if(mfile){
      do_mummap(mfile);
      list_remove(&mfile->elem);
      free(mfile);
  }
}


struct mmap_file* get_mmap_file(mapid_t id){
  struct thread* t = thread_current();
  struct list_elem* e;

  for (e = list_begin (&t->list_mmap); e != list_end (&t->list_mmap);
     e = list_next (e))
  {
    struct mmap_file *mmap = list_entry (e, struct mmap_file, elem);
    if(mmap->mapid == id){
      return mmap;
    }
  }

  return NULL;
}

//clear all opened memory-mapped-file
void
clear_opened_mmfiles(void){
  struct thread *t = thread_current ();
  while(!list_empty(&t->list_mmap)){
    do_mummap(list_entry(list_pop_front(&t->list_mmap), struct mmap_file, elem));
  }
}

//hash destory iterator function
void vm_destroy_func (struct hash_elem* e, void* _mfile)
{
  struct thread* t = thread_current();
  struct vm_entry *vme = hash_entry (e, struct vm_entry, mmap_elem);
  

  //delete hash element from thready vm
  //this entry is managed by mmap, not thread it self

  if(vme->page){
    ASSERT(vme->page->thread == t);
    del_page_from_lru_list(vme->page); 
    if(vme->swap_slot != SWAP_ERROR)
      swap_in(vme->swap_slot, NULL);
    free(vme->page);
  }

  hash_delete(&t->vm, &vme->elem);

  free(vme);  
}

bool mmap_vmentry_flush(struct page* page){
  struct thread* t = page->thread;
  struct vm_entry* vme = page->vme;

  if(!vme)
    return false;

  ASSERT(vme->mfile_id>=0);
  if(vme->mfile_id<0)
    return false;

  struct mmap_file * mfile = get_mmap_file(vme->mfile_id);
  ASSERT(mfile != NULL);
  if(!mfile)
    return false;

  if (vme->is_loaded){
    //write to file if memory is dirty
    if(pagedir_is_dirty(t->pagedir, vme->vaddr)){
      ASSERT(mfile->file != NULL);
      file_write_at(mfile->file, vme->vaddr, PGSIZE, vme->offset);
    }
  }

  return true;
}