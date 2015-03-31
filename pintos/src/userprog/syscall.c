#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "devices/shutdown.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *arg; /* 4 byte block */
  void *esp = f->esp;
  int number = *esp;
  printf ("system call!\n");

  check_address (esp);

  /* systemcall number is located in the top of esp */
  switch (number)
  {
    case SYS_HALT:
      halt ();
      break;
    case SYS_EXIT:
      arg = (int*) malloc (sizeof(int)*1);
      get_argument (esp, arg, 1);
      exit (arg[0]);
      break;
    case SYS_CREATE:
      arg = (int*) malloc (sizeof(int)*2);
      get_argument (esp, arg, 2);
      f->eax = create (arg[0], arg[1]);
      break();
    case SYS_REMOVE:
      arg = (int*) malloc (sizeof(int)*1);
      get_argument (esp, arg, 1);
      f->eax = remove (arg[0]);
      break;
  }

  if (arg)
    free (arg);
}

void check_address (void *addr)
{
  if ((uint32_t)addr < 0x8048000 || (uint32_t)addr > 0xc0000000)
  {
    printf ("userprog/syscall.c/check_address(): address %x fault", (uint32_t)addr);
    exit (-1);
  }
}

void get_argument (void *esp, int *arg, int count)
{
  int i;
  --esp;

  /* saving arguments in the user stack to the kernel */
  for (i=0; i<cnt; ++i)
  {
    check_address (esp);
    arg[i] = *esp;
    --esp;
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
