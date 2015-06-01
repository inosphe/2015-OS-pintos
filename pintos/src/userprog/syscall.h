#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

struct vm_entry;
typedef int pid_t;

void syscall_init (void);

void halt (void);
void exit (int status);

bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

int open(const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size, void* esp);
int write(int fd, void *buffer, unsigned size);
void seek (int fd , unsigned position);
unsigned tell (int fd);
void close (int fd);

// assignment2: system call

struct vm_entry* check_address (void *addr);
struct vm_entry* check_address2 (void* esp, void *addr);
static void get_argument (void *esp, int **arg, int count);

// project6: vm
void check_valid_buffer (void* buffer, unsigned size, void* esp, bool to_write);
void check_valid_string (const void* str, void* esp);

#endif /* userprog/syscall.h */
