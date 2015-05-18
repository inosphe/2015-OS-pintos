#include "mmap.h"
#include "page.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "threads/thread.h"


static void vm_destroy_func (struct hash_elem* e, void* aux);

mapid_t mmap (int fd, void *addr)
{
  static int id = 0;
  struct file* file = process_get_file(fd);
  struct thread* t = thread_current();
  struct mmap_file* mfile;
  off_t ofs = 0;
  uint32_t read_bytes=0;

  if(!file){
    return -1;
  }
  file = file_reopen(file);
  if(!file){
    return -1;
  }
  mfile = (struct mmap_file*)malloc(sizeof(struct mmap_file));
  mfile->mapid = id++;
  read_bytes = file_length(file);

  vm_init(&mfile->vm);
  mfile->vm.aux = mfile;
  
  while(read_bytes>0){
      struct vm_entry* vme = (struct vm_entry*)malloc (sizeof(struct vm_entry));
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

      insert_vme (&mfile->vm, vme);
      insert_vme (&t->vm, vme);

      ofs += page_read_bytes;
      addr += PGSIZE;
  }

  list_push_back(&t->list_mmap, &mfile->elem);
  
  return mfile->mapid;
}

void munmap (mapid_t id)
{
  struct mmap_file* mfile = get_mmap_file(id);
  if(mfile){
    file_close(mfile->file);
    hash_destroy (&mfile->vm, vm_destroy_func);  
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
  struct vm_entry *vme = hash_entry (e, struct vm_entry, elem);
  struct mmap_file * mfile = (struct mmap_file*)_mfile;

  if (vme->is_loaded)
    palloc_free_page (pagedir_get_page(t->pagedir, vme->vaddr));

  hash_delete(&mfile->vm, e);

  vme->is_loaded = false;
}