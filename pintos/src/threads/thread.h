#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <hash.h>
#include <stdint.h>
#include "threads/synch.h"


#define MAX_FILE_DESC_COUNT 64
struct file;

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

    /* for assignment 2 file descriptor */
    struct file* file_desc[MAX_FILE_DESC_COUNT];   /* my file dsecriptor table */
    int file_desc_size; /* table real size(max fd) + 1 */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

    /* for assignment4-4 (by yh) */
    int init_priority; // 스레드 생성시 설정된 우선순위를 저장
    struct lock *wait_on_lock; // 해당 스레드가 대기하고 있는 lock의 주소
    struct list donations; // 해당 스레드에 우선순위를 기부한 스레드들을 차례대로 저장하는 리스트
    struct list_elem donation_elem;

    /* for assignment2 */
    struct thread *parent;  /* parent process pointer */
    struct list child_list; /* linked list of child processes */
    struct list_elem child_elem; /* element struct for child_list */
    struct semaphore exit_program;
    struct semaphore load_program; 
    int load_status; /* when this process(or thread) load program to memory, set this value */
    int exit_status; /* when this process(or thread) dying, set this value */
    bool isExit;
    bool isLoad;

    int64_t   tick_to_awake;

    /* for mlfq */
    int nice;
    int recent_cpu;

    /* for virtual memory */
    struct hash vm;

    /* for manage mmap*/
    struct list list_mmap;
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* 실행중인 쓰레드를 sleep 상태로 전환 */
void thread_sleep(int64_t ticks);
/* 슬립 리스트에서 깨어나야 할 쓰레드를 깨움 */
void thread_awake(int64_t ticks);
/* 슬립 리스트에서 가장 짧은 틱을 계산 */
void update_next_tick_to_awake();
/* next_tick_to_awake를 반환 */
int64_t get_next_tick_to_awake(void);

bool tick_to_awake_less (const struct list_elem *, const struct list_elem *, void *);
bool cmp_priority (const struct list_elem *, const struct list_elem *, void *);
void donate_priority (void);
void remove_with_lock (struct lock *lock);

/*현재 수행중인 스레드와 가장 높은 우선순위의 스레드의 우선순위를 비교하여 스케쥴*/
void test_max_priority(void);
void refresh_priority (void);

/* Project 5: MLFQ */
void mlfqs_priority (struct thread *t);
void mlfqs_recent_cpu (struct thread *t);
void mlfqs_load_avg (void);
void mlfqs_increment (void);
void mlfqs_reclac (void);

#endif /* threads/thread.h */
