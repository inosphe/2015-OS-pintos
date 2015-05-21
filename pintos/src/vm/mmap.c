#include "mmap.h"
#include "page.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "threads/thread.h"


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
  list_push_back(&t->list_mmap, &mfile->elem);

  while(read_bytes>0){
      struct vm_entry* vme, *vme_check;

      if(addr){
        vme_check = find_vme(addr);
        if(vme_check){
          success = 0;
          break;
        }
      }

      vme = (struct vm_entry*)malloc (sizeof(struct vm_entry));
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      vme->type = VM_FILE;
      vme->is_loaded = false;
      vme->pinned = false;
      vme->file = file;
      vme->offset = ofs;
      vme->vaddr = (void*)addr;
      vme->read_bytes = page_read_bytes;
      vme->zero_bytes = page_zero_bytes;
      vme->writable = file_write_allowed(file);

      hash_insert (&mfile->vm, &vme->mmap_elem);
      insert_vme (&t->vm, vme);

      ofs += page_read_bytes;
      addr += PGSIZE;

      read_bytes -= page_read_bytes;
  }

  if(success){
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
    list_remove(&mfile->elem);
    free(mfile);
}

/* release mmap by mapid */
void munmap (mapid_t id)
{
  struct thread* t = thread_current();
  struct mmap_file* mfile = get_mmap_file(id);
  if(mfile){
      do_mummap(mfile);
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

void vm_destroy_func (struct hash_elem* e, void* _mfile)
{
  struct thread* t = thread_current();
  struct vm_entry *vme = hash_entry (e, struct vm_entry, mmap_elem);
  struct mmap_file * mfile = (struct mmap_file*)_mfile;

  hash_delete(&t->vm, &vme->elem);

  if (vme->is_loaded){
    if(pagedir_is_dirty(t->pagedir, vme->vaddr)){
      file_write_at(mfile->file, vme->vaddr, PGSIZE, vme->offset);
    }
    palloc_free_page (pagedir_get_page(t->pagedir, vme->vaddr));
    pagedir_clear_page(t->pagedir, vme->vaddr);
  }

  free(vme);  
}