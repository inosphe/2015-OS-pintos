#include "filesys/buffer_cache.h"
#include "filesys/filesys.h"
#include "threads/thread.h"

static const int BUFFER_CACHE_ENTRY_NB = 64;

static void* p_buffer_cache = NULL;
static struct buffer_head buffer_head_table[64];
static struct lock lock;

static void bc_flush_entry_not_synch (struct buffer_head *);

void bc_init (void){
	int i;
	memset(buffer_head_table, 0, sizeof(struct buffer_head) * BUFFER_CACHE_ENTRY_NB);	//clear buffer_head_table
	p_buffer_cache = malloc(BUFFER_CACHE_ENTRY_NB * BLOCK_SECTOR_SIZE);	//alloc memory for cache

	for(i=0; i<BUFFER_CACHE_ENTRY_NB; ++i){
		buffer_head_table[i].data = p_buffer_cache + i*BLOCK_SECTOR_SIZE;	//set cache entries to each buffer head
		lock_init(&buffer_head_table[i].lock);
	}

	lock_init(&lock);
}

void bc_term (void){
	bc_flush_all_entries();	//flush all entries before clear

	memset(buffer_head_table, 0, sizeof(struct buffer_head) * BUFFER_CACHE_ENTRY_NB);	//clear | not important
	free(p_buffer_cache);	//free memory
}

static bool alloc(struct buffer_head* pBuffer_h, block_sector_t id){
	ASSERT(pBuffer_h->data != NULL);

	lock_acquire(&pBuffer_h->lock);

	if(pBuffer_h->initialized){
		bc_flush_entry_not_synch(pBuffer_h);	//flush if allocated
	}

	pBuffer_h->initialized = true;
	pBuffer_h->dirty = false;
	pBuffer_h->accessed = true;		//prevent to release immediately
	pBuffer_h->sector = id;
	block_read (fs_device, pBuffer_h->sector, pBuffer_h->data);		//read sector data on init

	lock_release(&pBuffer_h->lock);

	return true;
}

struct buffer_head* bc_lookup (block_sector_t id){
	struct buffer_head* target_buffer_head = NULL;
	int i;

	lock_acquire(&lock);

	//find entry
	for(i=0; i< BUFFER_CACHE_ENTRY_NB; ++i){
		if(buffer_head_table[i].initialized && buffer_head_table[i].sector == id){
			target_buffer_head = &buffer_head_table[i];
			break;
		}
	}

	//if no empty slot found, select victim by LRU algorithm
	if(target_buffer_head == NULL){
		target_buffer_head = bc_select_victim();
	}

	ASSERT(target_buffer_head);

	//allocate
	if(target_buffer_head->sector != id){
		alloc(target_buffer_head, id);
	}

	ASSERT(target_buffer_head->sector == id);
	lock_release(&lock);

	return target_buffer_head;
}


bool bc_read (block_sector_t sector_idx, void* buffer, off_t bytes_read, int chunk_size, int sector_ofs){
	struct buffer_head* buffer_h = bc_lookup(sector_idx);
	lock_acquire(&buffer_h->lock);

	ASSERT(buffer);
	ASSERT(buffer_h->sector == sector_idx);
	ASSERT(buffer_h->initialized == true);
	ASSERT(buffer_h->data);

	//read from buffer
	memcpy(buffer+bytes_read, buffer_h->data+sector_ofs, chunk_size);

	lock_release(&buffer_h->lock);

	return true;
}

bool bc_write (block_sector_t sector_idx, void* buffer, off_t bytes_written, int chunk_size, int sector_ofs){
	struct buffer_head* buffer_h = bc_lookup(sector_idx);

	lock_acquire(&buffer_h->lock);

	ASSERT(buffer_h->sector == sector_idx);
	ASSERT(buffer_h->initialized == true);

	//write to buffer
	memcpy(buffer_h->data+sector_ofs, buffer+bytes_written, chunk_size);
	buffer_h->dirty = true;

	lock_release(&buffer_h->lock);

	return true;
}

struct buffer_head *bc_select_victim (void){
	int i;
	struct buffer_head* target;

	for(i=0; i<BUFFER_CACHE_ENTRY_NB; ++i){
		target = &buffer_head_table[i];
		if(!target->initialized || !target->accessed){	//find not accessed or initialized slot
			break;
		}

		target->accessed = false;	//mark not accessed

		if(i>=BUFFER_CACHE_ENTRY_NB){
			i = 0;
		}
	}

	return target;
}

void bc_flush_entry (struct buffer_head* buffer_h){
	lock_acquire(&buffer_h->lock);
	bc_flush_entry_not_synch(buffer_h);
	lock_release(&buffer_h->lock);
}

static void bc_flush_entry_not_synch (struct buffer_head * buffer_h){
	ASSERT(buffer_h);
	ASSERT(buffer_h->data);
	if(buffer_h->initialized && buffer_h->dirty)	//flush only if initilized && dirty
		block_write (fs_device, buffer_h->sector, buffer_h->data);
	buffer_h->dirty = false;
}

void bc_flush_all_entries (void){
	int i;

	lock_acquire(&lock);
	for(i=0; i<BUFFER_CACHE_ENTRY_NB; ++i){
		bc_flush_entry(&buffer_head_table[i]);	//flush all entries
	}
	lock_release(&lock);
}