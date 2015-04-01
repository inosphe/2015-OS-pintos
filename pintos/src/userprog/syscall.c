#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "devices/shutdown.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);
void halt (void);
void exit (int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *arg = 0;
  void *esp = 0;
  int number;
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
      arg = (int*) malloc (sizeof(int)*1);
      get_argument (esp, arg, 1);
      exit (arg[0]);
      break;

    case SYS_CREATE:
      printf ("SYS_CREATE called.\n");
      arg = (int*) malloc (sizeof(int)*2);
      get_argument (esp, arg, 2);
      check_address ((void*)arg[0]);
      f->eax = create ((const char*)arg[0], (unsigned)arg[1]);
      break;

    case SYS_REMOVE:
      printf ("SYS_REMOVE called.\n");
      arg = (int*) malloc (sizeof(int)*1);
      get_argument (esp, arg, 1);
      check_address ((void*)arg[0]);
      f->eax = remove ((const char*)arg[0]);
      break;

/* SYS_WRITE IS NOT USED FOR ASSIGNMENT 2. ONLY FOR TEST!
    case SYS_WRITE:
      printf ("SYS_WRITE called.\n");
      arg = (int*) malloc (sizeof(int)*3);
      get_argument (esp, arg, 3);
      printf (" %d, %s, %d\n", *(unsigned*)arg[0], (const char*)*(uint32_t*)arg[1], *(unsigned*)arg[2]);
*/
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
get_argument (void *esp, int *arg, int count)
{
  int i;
  /* saving address value of arguments in the user stack to the kernel ("arg" array) */
  for (i=0; i<count; ++i)
  {
    printf ("esp: %x\n", (uint32_t)esp);
    check_address (esp);
    arg[i] = *(int*)esp;
    printf ("arg[%d]: %x\n", i, &arg[i]);
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

