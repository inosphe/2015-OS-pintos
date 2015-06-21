#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCK_ENTRIES 124
#define INDIRECT_BLOCK_ENTRIES 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];  //point data sector
    block_sector_t indirect_block_sec;                      //point indirect map
    block_sector_t double_indirect_block_sec;               //point double-indirect map
  };

enum direct_t{
  NORMAL_DIRECT = 0
  , INDIRECT
  , DOUBLE_INDIRECT
  , OUT_LIMIT
};

struct sector_location{
  enum direct_t directness;  //enum value
  off_t index1;   //index in indirect
  off_t index2;   //index in double indirect
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock extend_lock;
  };

//update inode's file length
static bool inode_update_file_length(struct inode_disk* inode_disk, off_t start_pos, off_t end_pos);

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init(&lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      success = true;
      if(length > 0){
        success = inode_update_file_length(disk_inode, 0, length);  //update file length
      }

      bc_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
      
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->extend_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

//get inode's disk data
static bool get_disk_inode(const struct inode* inode, struct inode_disk* inode_disk){
  return bc_read(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0);
}

//get direct or indirect sector block by bytes pos
static void locate_byte(off_t pos, struct sector_location* sec_loc){
  off_t pos_sector = pos / BLOCK_SECTOR_SIZE;

  if(pos_sector < DIRECT_BLOCK_ENTRIES){  //when direct
    sec_loc->directness = NORMAL_DIRECT;
    sec_loc->index1 = pos_sector;
    sec_loc->index2 = 0;
  }
  else if(pos_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES){  //when indirect
    sec_loc->directness = INDIRECT;
    sec_loc->index1 = pos_sector - DIRECT_BLOCK_ENTRIES;
    sec_loc->index2 = 0;
  }
  else if(pos_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES*INDIRECT_BLOCK_ENTRIES){  //when double indirect
    sec_loc->directness = DOUBLE_INDIRECT;
    sec_loc->index1 = (pos_sector - DIRECT_BLOCK_ENTRIES) / INDIRECT_BLOCK_ENTRIES; //1st indirect sector block
    sec_loc->index2 = (pos_sector - DIRECT_BLOCK_ENTRIES) % INDIRECT_BLOCK_ENTRIES; //2nd indirect sector block
  }
  else{
    sec_loc->directness = OUT_LIMIT;
  }
}

//get block's index offset
static inline off_t map_table_offset (int index){
  return index * sizeof(block_sector_t);
}

static bool check_and_alloc_sector(block_sector_t* sector){
  if(*sector > 0){
    return true;
  }
  else{
    block_sector_t sector_idx;
    void* zeros;
    if(!free_map_allocate(1, &sector_idx)){ //alloc new sector
      return false;
    }

    zeros = malloc(BLOCK_SECTOR_SIZE);
    memset(zeros, 0, BLOCK_SECTOR_SIZE);

    *sector = sector_idx;
    bc_write(sector_idx, zeros, 0, BLOCK_SECTOR_SIZE, 0); //fill block by 0s

    free(zeros);
    return true;
  }
}

//set new sector id to inode's disk data
static bool register_sector (struct inode_disk* inode_disk, block_sector_t new_sector, struct sector_location sec_loc){
  block_sector_t sector_temp;
  switch(sec_loc.directness){
    case NORMAL_DIRECT:
      inode_disk->direct_map_table[sec_loc.index1] = new_sector;  //set memory only
      //printf("register direct[%u] = %u\n", sec_loc.index1, new_sector);
      break;
    case INDIRECT:
      check_and_alloc_sector(&inode_disk->indirect_block_sec);
      //write to indirect sector block
      bc_write(inode_disk->indirect_block_sec, &new_sector, 0, sizeof(block_sector_t), map_table_offset(sec_loc.index1)); 
      //printf("register indirect(%u)[%u] = %u\n", inode_disk->indirect_block_sec, sec_loc.index1, new_sector);
      break;
    case DOUBLE_INDIRECT:
      check_and_alloc_sector(&inode_disk->double_indirect_block_sec);
      //read double indirect sector block from disk
      bc_read(inode_disk->double_indirect_block_sec, &sector_temp, 0, sizeof(block_sector_t), map_table_offset(sec_loc.index1));
      //printf("register double_indirect(%u)[%u] = %u\n", inode_disk->double_indirect_block_sec, sec_loc.index1, sector_temp);

      if(sector_temp<=0){
        check_and_alloc_sector(&sector_temp); //alloc new sector block
        //write to double indirect sector block
        bc_write(inode_disk->double_indirect_block_sec, &sector_temp, 0, sizeof(block_sector_t), map_table_offset(sec_loc.index1));
      }

      //write to indirect sector block
      bc_write(sector_temp, &new_sector, 0, sizeof(block_sector_t), map_table_offset(sec_loc.index2));
      //printf("register double_indirect(%u)[%u]-%u = %u\n", inode_disk->double_indirect_block_sec, sec_loc.index2, sector_temp, new_sector);
      break;
  }

  //dont need to write inode_disk to bc?

  return true;
}

//get sector by byte offset
static block_sector_t byte_to_sector (const struct inode_disk* inode_disk, off_t pos){
  block_sector_t result_sec = -1;  //return sector id

  if(pos < inode_disk->length){
    struct inode_indirect_block* ind_block;
    struct sector_location sec_loc;
    locate_byte(pos, &sec_loc); //calculate index block offset

    switch(sec_loc.directness){
      case NORMAL_DIRECT: //when direct
        result_sec = inode_disk->direct_map_table[sec_loc.index1];
        break;
      case INDIRECT:  //when indirect, read sector id from disk
        bc_read(inode_disk->indirect_block_sec, &result_sec, 0, sizeof(block_sector_t), map_table_offset(sec_loc.index1));
        break;
      case DOUBLE_INDIRECT: //when indirect, read indirect sector id , and sector id from disk
        bc_read(inode_disk->double_indirect_block_sec, &result_sec, 0, sizeof(block_sector_t), map_table_offset(sec_loc.index1));
        bc_read(result_sec, &result_sec, 0, sizeof(block_sector_t), map_table_offset(sec_loc.index2));
        break;
    }
  }
  
  return result_sec;
}

/*
  add block sectors
  start_pos, end_pos : bytes
*/
static bool inode_update_file_length(struct inode_disk* inode_disk, off_t start_pos, off_t end_pos){
  int size = end_pos - start_pos;
  int offset = start_pos;
  int chunk_size;

  lock_acquire(&lock);

  while(size > 0){
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;
    block_sector_t sector_idx = 0;

    if(sector_ofs>0){
      //already allocated
      chunk_size = BLOCK_SECTOR_SIZE - sector_ofs;
    }
    else{
      struct sector_location sec_loc;
      
      chunk_size = BLOCK_SECTOR_SIZE;
      if(!check_and_alloc_sector(&sector_idx)){ //alloc new sector block
        return false;
      }
      locate_byte(offset, &sec_loc);
      register_sector(inode_disk, sector_idx, sec_loc);   //register new allocated sector block to inode's disk data
    }

    chunk_size = chunk_size>size?size:chunk_size;

    // printf("update file length | size(%d) offset(%d) chunk_size(%d)\n", size, offset, chunk_size);

    size -= chunk_size;
    offset += chunk_size;
  }
  lock_release(&lock);
  //printf("update file length2 | size(%d)\n", size);
  ASSERT(size == 0);
  ASSERT(start_pos + offset >= end_pos);
  return true;
}

//free all inodes
static void free_inode_sectors (struct inode_disk* inode_disk){
  int i, j;

  //free direct map's inodes
  for(i=0; i<DIRECT_BLOCK_ENTRIES; ++i){
    if(inode_disk->direct_map_table[i]>0){
      free_map_release(inode_disk->direct_map_table[i], 1);
    }
  }

  //free indirect direct map's inodes
  if(inode_disk->indirect_block_sec>0){
    block_sector_t sector_id;
    for(i=0; i<INDIRECT_BLOCK_ENTRIES; ++i){
      //read indirect map sector block id
      bc_read(inode_disk->indirect_block_sec, &sector_id, 0, sizeof(block_sector_t), map_table_offset(i));
      if(sector_id <= 0)
        break;  //no more sectors;

      free_map_release(sector_id, 1);
    }
    free_map_release(inode_disk->indirect_block_sec, 1);
  }

  //free double indirect direct map's inodes
  if(inode_disk->double_indirect_block_sec>0){
    block_sector_t sector_id, sector_id2;
    for(i=0; i<INDIRECT_BLOCK_ENTRIES; ++i){
      //read double indirect map sector block id
      bc_read(inode_disk->double_indirect_block_sec, &sector_id, 0, sizeof(block_sector_t), map_table_offset(i));

      if(sector_id <= 0){
        break;  //no more sectors;
      }

      for(j=0; j<INDIRECT_BLOCK_ENTRIES; ++j){
        //read indirect map sector block id
        bc_read(sector_id, &sector_id2, 0, sizeof(block_sector_t), map_table_offset(j));

        if(sector_id2 <= 0){
          break;  //no more sectors;
        }
        free_map_release(sector_id2, 1);
      }

      free_map_release(sector_id, 1);
    }
    free_map_release(inode_disk->indirect_block_sec, 1);
  }
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          struct inode_disk inode_disk;
          get_disk_inode(inode, &inode_disk);
          free_map_release (inode->sector, 1);  //free data blocks
          free_inode_sectors(&inode_disk);      //free inode block;
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  // printf("read at | inode(%p) sector(%u)\n", inode, inode->sector);
  lock_acquire(&inode->extend_lock);  //acquire inode's lock

  while (size > 0) 
    {
      struct inode_disk inode_disk;
      block_sector_t sector_idx;
      int sector_ofs;
      off_t inode_left;
      int sector_left;
      int min_left;
      int chunk_size;


      if(get_disk_inode(inode, &inode_disk) == false){  ;   //get inode_disk data from disk
        lock_release(&inode->extend_lock);
        return 0;
      }

      //printf("inode_disk : %x\n", inode_disk.magic);

      /* Disk sector to read, starting byte offset within sector. */
      sector_idx = byte_to_sector (&inode_disk, offset);
      sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      inode_left = inode_length (inode) - offset;
      sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      // printf("read | sector_idx(%u)\n", sector_idx);

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector from buffer into caller's buffer. */
          memset(buffer + bytes_read, 0, BLOCK_SECTOR_SIZE);
          bc_read(sector_idx, buffer, bytes_read, BLOCK_SECTOR_SIZE, sector_ofs);
        }
      else 
        {
          /* Read partial sector from buffer into caller's buffer. */
          bc_read(sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

    lock_release(&inode->extend_lock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  struct inode_disk inode_disk;
  off_t old_length;
  off_t write_end = offset + size;


  if(!get_disk_inode(inode, &inode_disk)){       //get inode_disk data from disk
    return 0;
  }

  if (inode->deny_write_cnt)
    return 0;

  old_length = inode_disk.length;

  lock_acquire(&inode->extend_lock);    //acquire inode's lock

  // printf("write at | inode(%p), sector(%u)\n", inode, inode->sector);

  if(write_end > old_length){ //extend file
    // printf("inode(%p) | update file length %u -> %u\n", inode, old_length, write_end);
    inode_update_file_length(&inode_disk, old_length, write_end);
    inode_disk.length = write_end;
    bc_write(inode->sector, &inode_disk, 0, BLOCK_SECTOR_SIZE, 0);
  }

  

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&inode_disk, offset);
      // printf("length(%u), size(%u), offset(%u), sector_idx(%u)\n", inode_disk.length, size, offset, sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length(inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      //printf("inode(%p) | write | offset(%u) sector(%u)\n", inode, offset, sector_idx);

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector to buffer */
          bc_write(sector_idx, buffer, bytes_written, BLOCK_SECTOR_SIZE, sector_ofs);
        }
      else 
        {
          /* Write partial sector to buffer */
          bc_write(sector_idx, buffer, bytes_written, chunk_size, sector_ofs);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  lock_release(&inode->extend_lock);

  bc_write(inode->sector, &inode_disk, 0, BLOCK_SECTOR_SIZE, 0);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk inode_disk;
  if(!get_disk_inode(inode, &inode_disk)){
    return 0;
  }
  return inode_disk.length;
}
