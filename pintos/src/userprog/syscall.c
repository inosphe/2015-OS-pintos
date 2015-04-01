#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <console.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "userprog/process.h"

#define EOF 0

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}

int
open(const char *file_name)
{
	struct file *file = filesys_open(file_name);
	int fd = -1;
	if(file != NULL)
	{
		return fd;
	}
  
  fd = process_add_file(file);
	return fd;
}

int
filesize (int fd)
{
  /* 파일 디스크립터를 이용하여 파일 객체 검색 */
  /* 해당 파일의 길이를 리턴 */
  /* 해당 파일이 존재하지 않으면 -1 리턴 */

  struct file *file = process_get_file(fd);
	if(file == NULL)
	{
		return -1;
	}
	return file_length(file);
}

int
read (int fd, void *buffer, unsigned size)
{
	/* 파일에 동시 접근이 일어날 수 있으므로Lock사용 */
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	/* 파일 디스크립터가 0일 경우 키보드에 입력을 버퍼에 저장 후 버퍼의 저장한 크기를 리턴 (input_getc() 이용) */
	/* 파일 디스크립터가 0이 아닐 경우 파일의 데이터를 크기만큼 저장 후 읽은 바이트 수를 리턴 */ 

	int i, ret;
	char c;
	struct file *file;

	if(fd == 0)
	{
		for(i=0; i<size; ++i)
		{
			c = input_getc();
			if(c == EOF)
				break;
			((char*)buffer)[i++] = c;
		}
		ret = i;
		return ret;
	}
	else
	{
		file = process_get_file(fd);
		if(file == NULL)
		{
			return 0;
		}
		
		file_lock(file);
		ret = file_read(file, buffer, size);
		file_unlock(file);

		return ret;
	}
}

int
write(int fd, void *buffer, unsigned size)
{
	/* 파일에 동시 접근이 일어날 수 있으므로Lock사용*/
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	/* 파일 디스크립터가 1일 경우 버퍼에 저장된 값을 화면에 출력후 버퍼의 크기 리턴 (putbuf() 이용) */
	/* 파일 디스크립터가 1이 아닐 경우 버퍼에 저장된 데이터를 크기
	만큼 파일에 기록후 기록한 바이트 수를 리턴 */


	struct file *file;
	int ret;
	if(fd == 1)
	{
		putbuf((const char*)buffer, size);
		ret = size;
		return ret;
	}
	else
	{
		file = process_get_file(fd);
		if(file == NULL)
		{
			return 0;
		}

		file_lock(file);
		ret = file_write(file, buffer, size);
		file_unlock(file);

		return ret;
	}
}

void
seek (int fd , unsigned position)
{
	struct file *file;
	file = process_get_file(fd);
	if(file == NULL)
	{
		return;
	}

	file_lock(file);
	file_seek(file, position);	//offset or position? 
	file_unlock(file);
}

unsigned
tell (int fd)
{
	struct file *file;
	unsigned pos;
	file = process_get_file(fd);
	if(file == NULL)
	{
		return 0;
	}

	file_lock(file);
	pos = file_tell(file);
	file_unlock(file);
}

void
close (int fd)
{
	struct file *file;
	file = process_get_file(fd);
	if(file == NULL)
	{
		return;
	}

	file_lock(file);
	file_close(file);
	file_unlock(file);
}
