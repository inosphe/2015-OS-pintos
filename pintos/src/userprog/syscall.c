
#include <stdio.h>
#include <stdlib.h>
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

#include "devices/shutdown.h"
#include "filesys/filesys.h"

#include "userprog/syscall.h"

static void syscall_handler (struct intr_frame *);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  #define ARG_INT ((int*)arg)[i++]
  #define ARG_UNSIGNED ((unsigned*)arg)[i++]
  #define ARG_CONST_CHAR ((const char**)arg)[i++]
  #define ARG_CUSTOM(_T_) ((T*)arg)[i++]

  int *arg = 0;
  void *esp = 0;
  int number;
  int i = 0;
  esp = f->esp;
  printf ("system call!\n");

  check_address (esp);
  number = *(int*)esp;

  printf ("esp: %x (%d)\n", (uint32_t)esp, *(uint32_t*)esp);
  esp -= 4;
  /* systemcall number is located in the top of user stack */
  switch (number)
  {
    case SYS_HALT:
      printf ("SYS_HALT called.\n");
      halt ();
      break;

    case SYS_EXIT:
      printf ("SYS_EXIT called.\n");
      get_argument (esp, &arg, 1);
      exit (ARG_INT);
      break;

    case SYS_CREATE:
      printf ("SYS_CREATE called.\n");
      get_argument (esp, &arg, 2);
      check_address ((void*)arg[0]);
      f->eax = create (ARG_CONST_CHAR, ARG_UNSIGNED);
      break;

    case SYS_REMOVE:
      printf ("SYS_REMOVE called.\n");
      get_argument (esp, &arg, 1);
      check_address ((void*)arg[0]);
      f->eax = remove (ARG_CONST_CHAR);
      break;

    case SYS_WRITE:
      printf ("SYS_WRITE called.\n");
      get_argument (esp, &arg, 3);
      f->eax = write(ARG_INT, ARG_CONST_CHAR, ARG_UNSIGNED);
      break;

    case SYS_SEEK:
      printf ("SYS_SEEK called.\n");
      get_argument (esp, &arg, 2);
      seek(ARG_INT, ARG_UNSIGNED);
      break;

    case SYS_TELL:
      printf ("SYS_TELL called.\n");
      get_argument (esp, &arg, 1);
      f->eax = tell (ARG_INT);
      break;

    case SYS_CLOSE:
      printf ("SYS_CLOSE called.\n");
      get_argument (esp, &arg, 1);
      close (ARG_INT);
      break;

  }
  if (arg)
    free (arg);
}

void
check_address (void *addr)
{
  if ((uint32_t)addr < 0x8048000 || (uint32_t)addr > 0xc0000000)
  {
    printf ("userprog/syscall.c/check_address(): address %x fault", (uint32_t)addr);
    exit (-1);
  }
}

void
get_argument (void *esp, int **arg, int count)
{
  int i;
  *arg = (int*) malloc (sizeof(int)*count);
  /* saving address value of arguments in the user stack to the kernel ("arg" array) */
  for (i=0; i<count; ++i)
  {
    printf ("esp: %x\n", (uint32_t)esp);
    check_address (esp);
    (*arg)[i] = *(int*)esp;
    printf ("arg[%d]: %x\n", i, &(*arg)[i]);
    esp -= 4;
  }
}

/* system shutdown */
void
halt (void)
{
  shutdown_power_off ();
}

/* exit current process */
void
exit (int status)
{
  struct thread *t = thread_current ();
  printf ("%s: exit(%d)\n", t->name, status);
  thread_exit ();
}

/* new file */
bool
create (const char *file, unsigned initial_size)
{
  if (filesys_create (file, initial_size))
    return true;
  else
    return false;
}

/* remove file */
bool
remove (const char *file)
{
  if (filesys_remove (file))
    return true;
  else
    return false;
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

