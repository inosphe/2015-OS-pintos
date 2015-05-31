#ifndef MMAP_H
#define MMAP_H

#include <list.h>
#include <hash.h>
#include "filesys/off_t.h"

struct page;

typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

struct mmap_file{
  mapid_t mapid;		//id
  struct file* file;	//file (reopened)
  struct list_elem elem;	//list_elem - see threads/list_mmap
  struct hash vm;			//own vm hash (element is duplicated with thread vm hash)
};

mapid_t mmap (int fd, void *addr);	//open
struct mmap_file* get_mmap_file(mapid_t id);	//get
void munmap (mapid_t);		//close

void clear_opened_mmfiles(void);	//close all

bool mmap_vmentry_flush(struct page* page);

#endif
