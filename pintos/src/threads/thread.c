#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#include "vm/page.h"

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

#define LOAD_AVG_DEFAULT 0
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0

int load_avg;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of processes in sleep, This processes are denoted by 'THREAD_BLOCKED'.*/
static struct list sleep_list;
/* Lowest value (tick_to_awake) in sleep_list */
static int64_t next_tick_to_awake = -1;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
//called only ONCE
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  //init lists
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&sleep_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  // Initialize file_desc array by 0. 
  // File descriptor can be released before exit thread.
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);
  load_avg = LOAD_AVG_DEFAULT;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);

  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  t->parent = thread_current ();
  tid = t->tid = allocate_tid ();
  

  list_push_back (&thread_current()->child_list, &t->child_elem);

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  //check current thread priority and schedule higher priority thread first
  test_max_priority();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, &t->elem, cmp_priority, 0); //insert to ready_list sorting by thread priority
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  struct thread *t = thread_current ();
  ASSERT (!intr_context ());

  //printf("%s: exit(%d)\n", t->name, t->exit_status);

#ifdef USERPROG
  process_exit ();
#endif


  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  t->status = THREAD_DYING;
  t->isExit = true;
  vm_destroy (&t->vm);
  sema_up (&t->exit_program);

  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered (&ready_list, &cur->elem, cmp_priority, 0);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if (!thread_mlfqs)
  {
    int prev_priority = thread_get_priority ();
    thread_current ()->init_priority = new_priority;
    refresh_priority ();

    //if new priority is higher, donate priority
    if (prev_priority < thread_get_priority ())
      donate_priority ();
    //otherwise, yield current preemption
    if (prev_priority > thread_get_priority ())
      test_max_priority();
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  enum intr_level old_level = intr_disable ();
  ASSERT (intr_get_level () == INTR_OFF);
  thread_current()->nice = nice;
  mlfqs_priority (thread_current());
  test_max_priority ();
  intr_set_level (old_level); 
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  int ret;
  enum intr_level old_level = intr_disable ();
  ASSERT (intr_get_level () == INTR_OFF);
  ret = thread_current()->nice;
  intr_set_level (old_level);
  return ret;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  int ret;
  enum intr_level old_level = intr_disable ();
  ASSERT (intr_get_level () == INTR_OFF);
  ret = fp_to_int_round(mult_mixed (load_avg, 100));
  intr_set_level (old_level);
  return ret;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  int ret;
  enum intr_level old_level = intr_disable ();
  ASSERT (intr_get_level () == INTR_OFF);
  ret = fp_to_int_round(mult_mixed (thread_current()->recent_cpu, 100));
  intr_set_level (old_level);
  return ret;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  t->parent = NULL;
  t->load_status = 0;
  t->exit_status = 0;
  sema_init (&t->load_program, 0);
  sema_init (&t->exit_program, 0);
  t->isExit = false;
  t->isLoad = false;
  memset(t->file_desc, 0, sizeof(struct file*) * MAX_FILE_DESC_COUNT);
  t->file_desc_size = 3;
  list_push_back (&all_list, &t->allelem);

  /* assignment2 */
  list_init (&t->child_list);

  /* assignment4-4 */
  t->wait_on_lock = NULL; // 해당 스레드가 대기하고 있는 lock의 주소
  t->init_priority = priority; // 스레드 생성시 설정된 우선순위를 저장한다.
  list_init (&t->donations); // 해당 스레드에 우선순위를 기부한 스레드들을 차례대로 저장하는 리스트

  /* sleep에서 깨어날 tick을 기술한다. sleep상태가 아니라면(초기값은) -1. */
  t->tick_to_awake = -1;

  /* assignment5 */
  t->nice = NICE_DEFAULT;
  t->recent_cpu = RECENT_CPU_DEFAULT;

  /* assignment6 */
  vm_init (&t->vm);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

void
thread_sleep(int64_t ticks)
{
  struct thread *cur = running_thread ();
  ASSERT (is_thread (cur));
  ASSERT (cur->status != THREAD_BLOCKED);
  
  cur->tick_to_awake = ticks;
  cur->status = THREAD_BLOCKED;
  list_insert_ordered(&sleep_list, &cur->elem, tick_to_awake_less, 0);
  update_next_tick_to_awake();

  schedule ();
}


void
thread_awake(int64_t ticks)
{
  struct list_elem* elem;
  struct thread* t;
  enum intr_level old_level;

  while(next_tick_to_awake>=0 && ticks >= next_tick_to_awake)
  {
    //printf("thread_awake | next_tick_to_awake(%lld), list_size(%d)\n", next_tick_to_awake, list_size(&sleep_list));
    old_level = intr_disable ();
    elem = list_pop_front(&sleep_list);
    t = list_entry(elem, struct thread, elem);


    //printf("popped : %lld\n", t->tick_to_awake);
    
    thread_unblock(t);
    update_next_tick_to_awake();

    intr_set_level (old_level);
  }
}

void update_next_tick_to_awake()
{
  struct list_elem *e;
  if(list_empty(&sleep_list))
  {
    next_tick_to_awake = -1;
  }
  else
  {
    e = list_begin(&sleep_list);
    struct thread *t = list_entry (e, struct thread, elem);
    next_tick_to_awake = t->tick_to_awake;
  }
}

int64_t get_next_tick_to_awake(void)
{
  return next_tick_to_awake;
}

/* Returns true if value A is less than value B, false
   otherwise. */
//for sorting sleep list
bool
tick_to_awake_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->tick_to_awake < b->tick_to_awake;
}


// compare thread priority for sorting ready_list
bool
cmp_priority (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->priority > b->priority; 
}


// test current thread priority with ready_list thread, if current thread's priority is lower, yield current preemption
void test_max_priority (void)
{
  struct thread *t;
  struct thread *cur = running_thread();

  if ( list_empty(&ready_list) || cur->status!=THREAD_RUNNING)
  {
    return;
  }

  cur = thread_current();

  t = list_entry(list_front(&ready_list), struct thread, elem);

  if (cur->priority < t->priority)
  {
    thread_yield();
  }
}

/* do priority donation */
void donate_priority (void)
{
  int depth = 1;
  struct thread *t = thread_current();

  // max nested depth = 8
  for (; depth <= 8; ++depth)
  {
    if (t->wait_on_lock == NULL) break;
    if (t->wait_on_lock->holder == NULL) break;

    /* in this case, priority of every next threads is higher than previous threads's. so, donation chain end.*/
    if (t->wait_on_lock->holder->priority >= t->priority) break;

    t->wait_on_lock->holder->priority = t->priority;
    t = t->wait_on_lock->holder;
  }
}

// remove from donation list
void remove_with_lock (struct lock *lock)
{
  struct list_elem *e = 0;
  struct list *l = &thread_current()->donations;
  struct thread *t = 0;

  for (e = list_begin (l); e != list_end (l); e = list_next (e))
  {
    t = list_entry (e, struct thread, donation_elem);
    if (t->wait_on_lock == lock)
      list_remove (e);
  }
}

//reset priority for update
void refresh_priority (void)
{
  struct list_elem *e = 0;
  struct list *l = &thread_current()->donations;
  struct thread *t = 0;

  thread_current()->priority = thread_current()->init_priority; //set current thread priority with init_priority
  
  for (e = list_begin (l); e != list_end (l); e = list_next (e))
  {
    t = list_entry (e, struct thread, donation_elem);
    if (t->priority > thread_get_priority())
      thread_current()->priority = t->priority;
  }
}

// priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
void mlfqs_priority (struct thread *t)
{
  // fixed-point float: recent_cpu
  // int: priority, PRI_MAX, nice
  if (t != idle_thread)
  {
    t->priority = PRI_MAX - fp_to_int( div_mixed(t->recent_cpu, 4) ) - (t->nice * 2);

    if (t->priority < PRI_MIN)
      t->priority = PRI_MIN;
    else if (t->priority > PRI_MAX)
      t->priority = PRI_MAX;
  }
}

// How many cpu time used by the thread recently
// recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
void mlfqs_recent_cpu (struct thread *t)
{ 
  // fixed-point float: recent_cpu, load_avg
  // int: nice
  if (t != idle_thread)
  {
    t->recent_cpu = add_mixed( mult_fp( div_fp( mult_mixed(load_avg, 2), 
                                                add_mixed(mult_mixed(load_avg, 2), 1))
                                        , t->recent_cpu),
                               t->nice);
  }
}

// average number of loadable processes (last one minute)
// load_avg = (59/60) * load_avg + (1/60) * ready_threads
void mlfqs_load_avg (void)
{
  int ready_threads = list_size (&ready_list);

  // current thread isn't idle_thread. so sum of ready threads and this thread.
  if (thread_current() != idle_thread)
    ready_threads += 1;

  load_avg = add_fp(
               mult_fp(div_fp(int_to_fp(59), int_to_fp(60)), load_avg), 
               mult_mixed(div_fp(int_to_fp(1), int_to_fp(60)), ready_threads)
             );

  ASSERT (load_avg >= 0)
}

// increase current priority 1
void mlfqs_increment (void)
{
  if (thread_current() != idle_thread)
    thread_current()->recent_cpu = add_mixed(thread_current()->recent_cpu, 1);
}

// recalculate the recent_cpu and priority of every thread
void mlfqs_recalc (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      mlfqs_recent_cpu (t);
      mlfqs_priority (t);
    }
}

