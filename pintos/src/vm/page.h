#ifndef PAGE_H
#define PAGE_H

#include <hash.h>
#include <list.h>
#include "mmap.h"
#include "threads/palloc.h"

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

struct file;
struct page;

struct vm_entry {

  uint8_t type; // VM_BIN, VM_FILE, VM_ANON
  void *vaddr; // virtual address that managed by vm_entry

  bool writable;
  bool is_loaded; // is physical memory loaded?
  bool pinned;  // if vm_entry is exist, then this value is false.

  struct file* file; // mapped file
  struct hash_elem mmap_elem;

  struct page* page;

  size_t offset;
  size_t read_bytes;
  size_t zero_bytes;
  size_t swap_slot;
  mapid_t mfile_id;

  struct hash_elem elem;

};

struct page{
  void* kaddr;
  struct vm_entry* vme;
  struct thread* thread;
  struct list_elem lru;
};


void vm_init (struct hash* vm);
void vm_destroy (struct hash* vm);

struct vm_entry* find_vme (void* vaddr);
bool insert_vme (struct hash* vm, struct vm_entry* vme);
bool delete_vme (struct hash* vm, struct vm_entry* vme);

void unpin_ptr (void* vaddr);
void unpin_string (void* str);
void unpin_buffer (void* buffer, unsigned size);

bool load_file (void* kaddr, struct vm_entry *vme);

struct vm_entry* alloc_vmentry(uint8_t type, void* vaddr);
struct page* alloc_page(enum palloc_flags);
bool page_set_vmentry(struct page* page, struct vm_entry* vme);
void free_page(struct page* page, bool preserve);

#endif
