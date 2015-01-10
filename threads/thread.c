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
#include "devices/timer.h"
#include "lib/kernel/list.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif


/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of process that are ready to be scheduled */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

//static int load_avg;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    /*
    Bottom-est frame in kernel thread
    eip is set to NULL when we create thread
    func is the function to be executed
    */
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

/* 
  kernel_thread is the bottom-est stack frame in thread
  It enables interrupts, call's the thread's function (passed 
  to thread_create())
  If the thread's function returns, it call's thread_exit() to terminate
  the thread
*/
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

/* Comparator function to keep the ready list in order */
bool less_ready_list (const struct list_elem *, const struct list_elem *, void * flag);
/* This function is called every second to update the load_avg */
void thread_set_load_avg(void);
/* This function returns the count of ready threads 
  + 1 (if running thread is not idle thread */
int ready_threads();

/* Increment the recent_cpu of running thread by 1 at each clock tick */
void increment_recent_cpu();

/* Updates the recent_cpu of all threads each second */
void update_recent_cpu();

/* Passed to thread_foreach to update each threads recent_cpu */
void update_recent_cpu_each(struct thread *,void *);

/* Updates the priority of each thread every four clock ticks */
void update_priority(struct thread *, void *);
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
//TODO : Who calls thread_init? 
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  /* Set up a thread structure for the running thread. */
  /*
    running_thread determines the current thread by means of
    current stack pointer. It rounds it down to the start of the page
    since 'struct thread' is always at the beginning of a page
  */
  /* 
    TODO: 
    Current esp points to an address which gives us current thread
    Q: How is the page allocated to which esp points to?
  */
  initial_thread = running_thread ();

  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  /* recent_cpu and nice value for initial thread are zero */
  initial_thread->recent_cpu = 0;
  initial_thread->nice = 0;
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
  
  /* load_avg is initialized with 0 */
  load_avg = 0;
  
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
  /* Work needed to be done for advanced scheduler
     It doesn't need synchronization as we are in interrupt handler
     which can't be interrupted in pintos
  */
  if (thread_mlfqs)
  {
    // Increments recent cpu of current thread at each tick
    increment_recent_cpu();
    
    /*
      Each second: 
      update load average (global)
      update recent_cpu of each thread
    */
    if(timer_ticks()%100 ==0)
    {
      thread_set_load_avg();
      update_recent_cpu();
    }
    // Every 4 timer ticks, updates priority of all threads
    // Sorts the ready list as per priority
    if(timer_ticks() % 4 ==0)
    { 
      thread_foreach(update_priority,NULL);
      if (!list_empty(&ready_list))
      {
        // Flag to choose comparator function '<' instead of '<='
        bool flag = false;
        list_sort(&ready_list,less_ready_list,&flag);
      }
    }

  }
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

void
update_priority(struct thread * t, void * aux)
{
  if (t == idle_thread)
    return;
  
  // Evaluation priority = PRI_MAX - (recent_cpu/4) - (nice*2)
  int priority = int_to_fp(PRI_MAX);
  int recent_cpu = div_mixed(t->recent_cpu,4);
  int nice = t->nice*2;
  int x = add_mixed(recent_cpu,nice);
  
  priority = sub_fp(priority,x);
  // Sets t->priority = PRI_MAX - (recent_cpu/4) - (nice*2) 
  t->priority = fp_to_int_round(priority);
  
  // Brings t->priority in range if it escalates MIN/MAX
  if (t->priority < PRI_MIN)
    t->priority = PRI_MIN;
  if (t->priority>PRI_MAX)
    t->priority = PRI_MAX;
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
  tid = t->tid = allocate_tid ();
  t->fd_count =1;
  list_init (&t->file_list);
  /* recent_cpu and nice values of newly created 
      thread are inherited from parent thread */

  t->recent_cpu = thread_current()->recent_cpu;
  t->nice = thread_current()->nice;
  
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
  //TODO: What is this? I mean the syntax
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);
  
  /* If newly created thread has higher priority 
     then parent thread yield */
  old_level = intr_disable();
  if (thread_current()->priority < t->priority)
    thread_yield();
  intr_set_level(old_level);
  
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
  /* Flag = true allows us to choose '<=' comparator instead of '<'
      to enable round-robin scheduling of same priority threads*/
  bool flag = true;
  /* Insert the thread being unblock in the ready list in order w.r.t priority */
  list_insert_ordered(&ready_list,&t->elem,less_ready_list,&flag);
    
  t->status = THREAD_READY;
  intr_set_level(old_level);

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
  ASSERT (!intr_context ());

//TODO: What does this block do?
#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  /* 
  NOT_REACHED just fires as assertion if it gets executed
  It should never execute
  */
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
  { 
    bool flag = true;
    list_insert_ordered(&ready_list, &cur->elem,less_ready_list,&flag);
  }
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
  // If Advanced Scheduler is enabled, priority can't be set directly
  if (thread_mlfqs == true)
    return;
  
  /* new_priority does not take effect until the thread has released
  all the priority it has received under donation */
  struct thread * curr = thread_current();
  if(curr->original_priority == curr->priority)
    curr->original_priority = curr->priority = new_priority;
  else
    curr->original_priority = new_priority;
  
  if (list_empty(&ready_list))
    return;
  /*
    Get highest priority thread from ready list
    and compare. 
    If current thread's priority is lower 
    than highest priority thread, yield
  */
  
  enum intr_level old_level = intr_disable();
  struct list_elem * e = list_back (&ready_list);
  struct thread * t = list_entry(e, struct thread, elem);
  if ( new_priority < t->priority)
    thread_yield();
  intr_set_level(old_level);  
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  enum intr_level old_level = intr_disable();
  int tmp = thread_current ()->priority;
  intr_set_level(old_level);
  return tmp;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  enum intr_level old_level = intr_disable();
  thread_current()->nice = nice;
  update_priority(thread_current(),NULL);
  if(!list_empty(&ready_list))
  {  
    struct list_elem * e = list_back (&ready_list);
    struct thread * t = list_entry(e, struct thread, elem);
    if ( thread_current()->priority < t->priority)
      thread_yield();
  }
  intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  enum intr_level old_level = intr_disable();
  int tmp = thread_current()->nice;
  intr_set_level(old_level);
  return tmp;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  enum intr_level old_level = intr_disable();
  int tmp = mult_mixed(load_avg,100);
  tmp = fp_to_int_round(tmp);
  intr_set_level(old_level);
  return tmp;
}

void
thread_set_load_avg (void)
{
  /* This function is called from an interrupt handler
    Hence, No need to turn interrupts off */
  int term1 = div_mixed( mult_mixed(load_avg,59) , 60);
  int term2 = div_mixed ( mult_mixed( int_to_fp(ready_threads()) ,1) , 60);
  
  load_avg = add_fp(term1, term2);

}

/* Return the total number of ready threads + 1 
  if the current thread is not the ideal thread */
int
ready_threads(void)
{

  int n = list_size(&ready_list);
  
  if(thread_current() != idle_thread)
    n++;
  return n;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  enum intr_level old_level = intr_disable();
  struct thread * t = thread_current();
  int tmp = mult_mixed(t->recent_cpu,100);
  tmp = fp_to_int_round(tmp);
  intr_set_level(old_level);
  return tmp;
}

/* Increments the recent_cpu of running thread by 1*/
void
increment_recent_cpu()
{
  /* This func is called from timer interrupt handler
    Hence no need to turn the interrupts off */
  
  if(thread_current() == idle_thread)
    return;
  struct thread * t = thread_current();
  t->recent_cpu = add_mixed(t->recent_cpu,1);
  return;
}

void
update_recent_cpu()
{
  thread_foreach(update_recent_cpu_each, NULL);
}

/* Updates recent_cpu of each thread every second */
void update_recent_cpu_each (struct thread * t, void * aux)
{
  /* This func is called from timer interrupt handler
    Hence no need to turn the interrupts off */
  
  /* Evaluating:
    recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice
  */
  int a = mult_mixed(load_avg,2);;
  int b = add_mixed(a,1);


  int coeff = div_fp(a,b);
  t->recent_cpu = mult_fp(coeff,t->recent_cpu);
  t->recent_cpu = add_mixed(t->recent_cpu,t->nice);
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
          TODO: Where are the interrupts being turned on?
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
  if(!thread_mlfqs)
    t->priority = priority;
  t->original_priority = priority;
  list_init(&t->donors_list);
  t->wait_lock = NULL;
  t->recent_cpu = 0;
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);
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
  {
    /* Returns the highest priority thread
       since ready_list is ordered in asceding order
       Returns the last thread in the list */
    return list_entry (list_pop_back (&ready_list), struct thread, elem);
  }
  
}

/* Comparator function for sorting and inserting in order in ready list
    If * flag == false, we use '<=' else we use '<'
    First one is for enabling round-robin scheduling
    Second one is for sorting the ready list
*/
bool less_ready_list ( const struct list_elem *a, const struct list_elem *b, void * flag) 
{
  struct thread * t_a = list_entry (a,struct thread, elem);
  struct thread * t_b = list_entry (b, struct thread, elem);
  bool * cmp_flag = (bool *) flag;
  if (*cmp_flag == true)
  {
    if (t_a->priority <= t_b->priority)
        return true;
    else
      return false;
  }
  else
  {
    if (t_a->priority < t_b->priority)
      return true;
    else
      return false;
  }
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
  //TODO: What does process_activate do?
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
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
