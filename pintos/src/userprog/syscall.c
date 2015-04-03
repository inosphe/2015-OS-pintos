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
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"

#define EOF 0
static void syscall_handler (struct intr_frame *);
bool user_mem_read(void *, void *, int);
static int get_user(const uint8_t *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  #define ARG_INT ((int*)arg)[i--]
  #define ARG_UNSIGNED ((unsigned*)arg)[i--]
  #define ARG_CONST_CHAR ((const char**)arg)[i--]
  #define ARG_CUSTOM(_T_) ((T*)arg)[i--]

  #define DECL_ARGS(count) i = count-1; get_argument (esp, &arg, count);

  int *arg = 0;
  void *esp = 0;
  int number;
  int i = 0;
  esp = f->esp;
  check_address (esp);
  number = *(int*)esp;
  /* systemcall number is located in the top of user stack */
  printf("syscall %x : %d\n", esp, number);
  esp += 4;
  switch (number)
  {
    case SYS_HALT:
      halt ();
      break;

    case SYS_EXIT:
      DECL_ARGS(1)
      exit (ARG_INT);
      break;

    case SYS_CREATE:
      DECL_ARGS(2)
      f->eax = create (ARG_CONST_CHAR, ARG_UNSIGNED);
      break;

    case SYS_REMOVE:
      DECL_ARGS(1)
      f->eax = remove (ARG_CONST_CHAR);
      break;

    case SYS_EXEC:
      DECL_ARGS(1)
      f->eax = exec (ARG_CONST_CHAR);
      break;

    case SYS_WAIT:
      DECL_ARGS(1)
      f->eax = wait (ARG_INT);
      break;

    case SYS_OPEN:
      DECL_ARGS(1)
      f->eax = open(ARG_INT);
      break;

    case SYS_FILESIZE:
      DECL_ARGS(1);
      f->eax = filesize(ARG_INT);
      break;

    case SYS_WRITE:
      DECL_ARGS(3)
      f->eax = write(ARG_INT, ARG_CONST_CHAR, ARG_UNSIGNED);
      break;

    case SYS_READ:
      DECL_ARGS(3)
      f->eax = read(ARG_INT, ARG_CONST_CHAR, ARG_UNSIGNED);
      break;    

    case SYS_SEEK:
      DECL_ARGS(2)
      seek(ARG_INT, ARG_UNSIGNED);
      break;

    case SYS_TELL:
      DECL_ARGS(1)
      f->eax = tell (ARG_INT);
      break;

    case SYS_CLOSE:
      DECL_ARGS(1)
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
    exit (-1);
  }
}

void
static get_argument (void *esp, int **arg, int count)
{
  int i;
  //esp += 4*(1+count);
  *arg = (int*) malloc (sizeof(int)*count);
  /* saving address value of arguments in the user stack to the kernel ("arg" array) */
  for (i=0; i<count; ++i)
  {
    user_mem_read(esp, *arg+i, 4);
    esp += 4;
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
  t->exit_status = status;
  thread_exit ();
}

/* new file */
bool
create (const char *file, unsigned initial_size)
{
  if (filesys_create (file, initial_size))
  {
    return true;
  }
  else
  {
    return false;
  }
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
	if(file == NULL)
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

  // printf("#%d\n", fd);
  // printf("#%s\n", buffer);
  // printf("#%u\n", size);


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
  return pos;
}

void
close (int fd)
{
	process_close_file(fd);	
}

pid_t exec (const char *cmd_line)
{
  struct thread *t = thread_current ();
  struct thread *child = 0;
  struct list_elem *e = 0;
  pid_t child_pid;

  child_pid = (pid_t) process_execute (cmd_line);
  
  if (child_pid == TID_ERROR)
  {
    sema_down (&t->load_program);
    return -1;
  }
 
  
  for (e = list_begin (&t->child_list); e != list_end (&t->child_list);
       e = list_next (e))
  {
    child = list_entry (e, struct thread, child_elem);
    if (child->tid == child_pid)
      break;
  }
  
  sema_down (&t->load_program);

  if (child->load_status == -1)
    return -1;

  return child_pid;
}

int wait (tid_t tid)
{
  return process_wait(tid);
}

bool
user_mem_read(void *src, void *des, int bytes)
{
  //printf("user_mem_read src(%x), des(%x), bytes(%d)\n", src, des, bytes);
  int value, i;
  for(i=0; i<bytes ; i++){
    value = get_user(src+i);
    //printf("value : %x\n", value);
    if(value==-1)
      return false;
    *(char*)(des+i) = value&0xff;
  }
  // printf("des : %x\n", *(int*)des);
  return true;
}

static int
get_user (const uint8_t *uaddr)
{
  check_address (uaddr);

  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
          : "=&a" (result) : "m" (*uaddr));
  return result;
}


