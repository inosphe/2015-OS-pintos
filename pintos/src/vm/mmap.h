#ifndef MMAP_H
#define MMAP_H

#include <list.h>
#include <hash.h>
#include "filesys/off_t.h"

typedef int mapid_t;
struct mmap_file{
  mapid_t mapid;
  struct file* file;
  struct list_elem elem;
  struct hash vm;
};

mapid_t mmap (int fd, void *addr);
struct mmap_file* get_mmap_file(mapid_t id);
void munmap (mapid_t);

#endif
