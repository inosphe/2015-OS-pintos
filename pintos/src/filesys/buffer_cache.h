#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include "filesys/inode.h"
#include "threads/synch.h"

struct buffer_head{
	struct inode*	inode;			//not used
	bool 			dirty;			//true if modified
	bool 			initialized;	//true if allocated
	bool 			accessed;		//true is accessed | for LRU
	block_sector_t	sector;			//block sector id
	void*			data;			//data pointer
	struct lock 	lock;			//lock for synch
};

void bc_init (void); //Buffer cache를 초기화하는 함수
bool bc_read (block_sector_t, void*, off_t, int, int); //Buffer cache에서 요청 받은 buffer frame을 읽어옴
bool bc_write (block_sector_t, void*, off_t, int, int); //Buffer cache의 buffer frame에 요청 받은 data를 기록
struct buffer_head* bc_lookup (block_sector_t sector_idx); //버퍼캐시를 순회하며 target sector가 있는지 검색
struct buffer_head* bc_select_victim (void); //버퍼캐시에서 victim을 선정하여 entry head 포인터를 반환
void bc_flush_entry (struct buffer_head *); //인자로 주어진 entry의 dirty비트를 false로 􏳱팅하면서 해당 내역을 disk로 flush
void bc_flush_all_entries (void); //버퍼캐시를 순회하면서 dirty비트가 true인 entry를 모두 디스크로 flush
void bc_term (void); //모든 dirty entry flush 및 buffer cache 해지

#endif

