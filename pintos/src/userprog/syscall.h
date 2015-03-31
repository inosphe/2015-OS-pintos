#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

// assignment2: system call

void check_address (void *addr);
void get_argument (void *esp, int *arg, int count);

#endif /* userprog/syscall.h */
