#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/mmap.h"
#include "vm/swap.h"

const int MAX_STACK_SIZE = 8 * 1024 * 1024;

/* push 8bit type value to stack */
#define push_stack_int8(addr, offset, val) \
  { \
    offset += 1; \
    *((int8_t*)(addr - offset)) = (int8_t)val; \
  } 

/* push 32bit type value to stack */
#define push_stack_int32(addr, offset, val) \
  { \
    offset += 4; \
    *((int32_t*)(addr - offset)) = (int32_t)val; \
  } 

/* push continuous character string value to stack */
#define push_stack_string(addr, offset, val) \
  { \
    t = strlen(val) + 1;  \
    offset += t; \
    memcpy(addr - offset, val, t); \
  } 

/* set esp as addr - offset */
#define set_esp(addr, offset) \
  { \
    *esp = addr - offset; \
  }

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name_) 
{
  char *fn_copy = NULL;
  char *lasts;
  char *file_name = NULL;
  tid_t tid;

  /* This page is owned & managed by 'start_process' */
  fn_copy = palloc_get_page (0);
  if(!fn_copy)
    goto ERROR_HANDLE;
  strlcpy (fn_copy, file_name_, PGSIZE-1);

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  file_name = palloc_get_page (0);
  if(!file_name)
    goto ERROR_HANDLE;
  strlcpy (file_name, file_name_, PGSIZE-1);

  file_name = strtok_r(file_name, " ", &lasts); //set filename using first token of file_name.
  
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    goto ERROR_HANDLE;

  /* This is copied in thread_create-thread_init*/
  palloc_free_page(file_name);

  return tid;


ERROR_HANDLE:
  if(fn_copy != NULL)
    palloc_free_page(fn_copy);

  if(file_name != NULL)
    palloc_free_page(file_name);
  
  ASSERT(false);
  return TID_ERROR;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name;
  struct intr_frame if_;
  char *lasts;
  bool success;
  struct page* arguments_pages[32];
  char *parse[32];
  char count;
  int i;
  char *parse_temp;
  struct thread *parent = thread_current ()->parent;

  count = 0;
  for(parse_temp = strtok_r(file_name_, " ", &lasts);
    parse_temp != NULL;
    parse_temp = strtok_r(NULL, " ", &lasts))
  {
    arguments_pages[count] = alloc_page(PAL_USER);
    parse[count] = arguments_pages[count]->kaddr;
    if(strlen(parse_temp) >= PGSIZE-1)
      printf("@ %s | each argument length must be lower than %d.\n", parse_temp, PGSIZE-1);

    strlcpy(parse[count], parse_temp, PGSIZE-1);
    ++count;

    if(count == 32)
      break;
  }

  if(count > 0)
    file_name = parse[0];
  else
    file_name = NULL;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load (file_name, &if_.eip, &if_.esp);
  //printf("success : %d\n", success);
  
  /* If load failed, quit. */
  if (!success)
  {
    thread_current ()->load_status = -1;
    thread_current ()->exit_status = -1;
    
  }
  else{
    thread_current ()->load_status = 1;
    argument_stack(parse, count, &if_.esp);
    
    /* free parse memories */
    for (i = 0; i < count; ++i)
    {
      free_page(arguments_pages[i], false);
    }

    //hex_dump(if_.esp, if_.esp, PHYS_BASE - if_.esp, true);    
  }

  sema_up(&parent->load_program);
  
  palloc_free_page (file_name_);

  if(!success){
    thread_exit ();
  }  

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *child;
  struct thread *t = thread_current ();
  int ret = -1;

  child = get_child_process (child_tid);
  if (child == NULL)
    return -1;

  sema_down (&child->exit_program);
  ret = child->exit_status;
  remove_child_process (child);
  return ret;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  clear_opened_mmfiles();   //clear all opened memory-mapped-file
  vm_destroy (&cur->vm);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
  {
    /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate (NULL);
    pagedir_destroy (pd);
  } 

  clear_opened_filedesc();  //clear all opened file 
    
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}
/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  process_add_file(file);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);


  //printf("* Load_segment | %d, %d\n", read_bytes, zero_bytes);
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      struct vm_entry* vme = alloc_vmentry(VM_BIN, (void*)upage);

      //printf("load_segment | %d, %d\n", read_bytes, zero_bytes);
      //printf("> %p | %d, %d, %d\n", upage, page_read_bytes, page_zero_bytes, ofs);

      /* vm_entry fields init */
      vme->is_loaded = false;
      vme->pinned = false;
      vme->file = file;
      vme->offset = ofs;
      vme->read_bytes = page_read_bytes;
      vme->zero_bytes = page_zero_bytes;
      vme->writable = writable;

      /* insert new vm_entry to the vm table */
      insert_vme (&thread_current()->vm, vme);

/*
      >>Get a page of memory.
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      >>Load this page.
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      >>Add the page to the process's address space.
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        } 
*/
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
      //printf("> %d, %d\n", read_bytes, zero_bytes);
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  
  bool success = false;
  struct page* kpage;
  struct vm_entry* vme;

  kpage = alloc_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
  {
    struct vm_entry* vme = alloc_vmentry(VM_ANON, PHYS_BASE - PGSIZE);
    if(vme){
      vme->writable = true;
      if (page_set_vmentry(kpage, vme))
      {
        *esp = PHYS_BASE;
        success = true;
      }
    }
  }

  if(!success){
    if(vme) delete_vme(&thread_current()->vm, vme);
    if(kpage) free_page (kpage, false);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */


void
argument_stack(char **parse ,int count ,void **esp)
{

  int i;
  int t;
  uint32_t data_size = 0;

  void *addr0; //argv's contents start address
  uint8_t offset0;  //argv's contents offset
  void *addr1 ; //argvs start address
  uint8_t offset1; //argvs offset
  void *argv;

  addr0 = *esp;
  offset0 = 0;

  //calculate sum of arguments data size
  for(i=count-1; i>=0; --i)
  {
    data_size += strlen(parse[i]) + 1;          //strlen + '\0'
  }

  addr1 = (void*)(((uint32_t)addr0 - data_size) & 0xfffffffc);     //for word-align(4Byte)
  offset1 = 0;

  push_stack_int32(addr1, offset1, 0);                //push argv[argc]; 

  for(i=count-1; i>=0; --i)
  {
    push_stack_string(addr0, offset0, parse[i]);      //push argv[i][j](string)
    push_stack_int32(addr1, offset1, addr0-offset0);  //push argv[i]
  }

  while(addr0-offset0>addr1)
  {
    push_stack_int8(addr0, offset0, 0);    //push zero for word-align
  }

  argv = addr1-offset1;

  push_stack_int32(addr1, offset1, argv);   //push argv
  push_stack_int32(addr1, offset1, count);  //push argc
  push_stack_int32(addr1, offset1, 0);      //push fake address(zero) for return address

  set_esp(addr1, offset1);                  //set esp value using addr1 - offset1
}

void
clear_opened_filedesc(void)
{
  int i;
  struct thread *t = thread_current ();

  for(i=3; i<t->file_desc_size; ++i)
  {
    process_close_file(i);
  }
}


// search and get file struct by file descriptor
struct file*
process_get_file(int fd)
{
  struct thread *t = thread_current ();
  if(fd<0 || fd >= t->file_desc_size || t->file_desc[fd]==NULL)
    return NULL; // not found

  //printf("process_get_file(%d) : %x\n", t->file_desc[fd]);

  return t->file_desc[fd];
}

int
process_add_file (struct file *f)
{
  int fd = -1;
  struct thread *t = thread_current ();
  //printf("process_add_file %d, %d\n", t->file_desc_size, MAX_FILE_DESC_COUNT);
  if(t->file_desc_size >= MAX_FILE_DESC_COUNT)
  {
    //File desc array is full.
    printf("file_desc_size is overed MAX_FILE_DESC_COUNT(%d)\n", MAX_FILE_DESC_COUNT);
    return fd;
  }

  fd = t->file_desc_size++;

  //Is it need to copy memory?
  t->file_desc[fd] = f;

  //printf("fd : %d\n", fd);
  return fd;
}

void
process_close_file (int fd)
{
  struct thread *t = thread_current ();
  struct file *file = process_get_file(fd);
  if(file != NULL)
  {
    file_close(file);
    t->file_desc[fd] = NULL;
  }
}

struct thread *get_child_process (int pid)
{
  struct thread *t;
  struct thread *child;
  struct list_elem *e;
  t = thread_current();

  for (e = list_begin (&t->child_list); e != list_end (&t->child_list);
       e = list_next (e))
  {
    child = list_entry (e, struct thread, child_elem);
    if (child->tid == pid)
      return child;
  }
  return NULL;
}

void remove_child_process (struct thread *cp)
{

  struct thread *t;
  struct thread *child;
  struct list_elem *e;
  t = thread_current();
  for (e = list_begin (&t->child_list); e != list_end (&t->child_list);
       e = list_next (e))
  {
    child = list_entry (e, struct thread, child_elem);
    if (child == cp)
    {
      e->prev->next = e->next;
      e->next->prev = e->prev;
      palloc_free_page (cp);
      break;
    }
  }
}

// page fault handler
bool handle_mm_fault (struct vm_entry *vme)
{
  struct page* page = NULL;
  struct thread* t = thread_current();
  bool ret = true;


  if(vme == NULL){
    printf("no vme\n");
  }

  
  lock_acquire(&t->lock_vme);

  //when fault raised if vme is already loaded, it's error
  if(vme->is_loaded){
    printf("already loaded\n"); 
    lock_release(&t->lock_vme);
    return false;
  }

  //allocate physical page frame from User pool
  page = alloc_page(PAL_USER|PAL_ZERO);
  if (!page){
    printf("page null\n");
    lock_release(&t->lock_vme);
    return false;
  }

  if(!page_set_vmentry(page, vme)){
    printf("page install failed.\n");
    lock_release(&t->lock_vme);
    return false;
  }

  ASSERT(vme->is_loaded == true);

  //printf("page : %p\n", page);
  //printf("page->kaddr(%p)\n", page->kaddr);

  /* VM constants are defined in page.h */

  switch (vme->type)
  {
    //load from ELF binary file
    case VM_BIN:
      if (!load_file (page->kaddr, vme))
        ret = false;
      break;

    /* for memory mapped file */
    case VM_FILE:
      if (!load_file (page->kaddr, vme))
        ret = false;
      break;
    case VM_ANON:
      if(page->vme->swap_slot!=SWAP_ERROR){
        swap_in(page->vme->swap_slot, page->kaddr);
      }
      page->vme->swap_slot = SWAP_ERROR;
      break;

    default:
      printf("invalid type\n");
      ret = false;
      break;
  }

  lock_release(&t->lock_vme);

  //ASSERT(vme->is_loaded == true);

  return ret;
}


//expand stack - alloc vm entry
struct vm_entry* expand_stack(void* addr){
  struct vm_entry* vme;
  vme = alloc_vmentry(VM_ANON, pg_round_down (addr));
  vme->writable = true;
  return vme;
}

//check stack heuristic
bool verify_stack(void* esp, void* addr){
  bool ret;
  bool ret2;
  bool ret3;

  void* esp2 = esp;

  ret = ((addr+32) >= esp2);
  ret2 = (addr<PHYS_BASE);
  ret3 = (PHYS_BASE<MAX_STACK_SIZE+esp);

  // printf("verify_stack esp(%p), addr(%p)\n", esp, addr);
  //  printf("%d, %d, %d\n", ret, ret2, ret3);

  return ret && ret2 && ret3;
}