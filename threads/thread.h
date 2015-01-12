#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "synch.h"

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
    
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif
  	
		/* DATA STRUCTURES FOR ALARM CLOCK */  
		    
		int64_t sleep_time;
    struct list_elem sleep_list_elem;
		
		/* DATA STRUCTURES FOR PRIORITY SCHEDULING */   
		 
		/*
    original_priority - 
    To restore priority after releasing donated/received priority
    */
    int original_priority;
    
    /* 
    To determine the lock the thread is waiting for if any
    It would allow us to recursively/iteratively donate priority
    */
    struct lock * wait_lock;

    /*
    List of possible donor threads
    It would allow the thread to release the donated/received priority 
    when releasing the lock, by clearing the wait_lock element
    of the donor threads waiting on the currently released lock
    */
    struct list donors_list;
  
    /*
    donor_elem would allow this thread to add itself to the donors list
    of the thread it is donating priority
    */
    struct list_elem donor_elem;    
		
		/* DATA STRUCTURES FOR ADVANCED SCHEDULER */
		int recent_cpu;                     /* Recent CPU Acquisition */
    
    int nice;                           /* Nice-ness of this thread 
                                           to let go CPU for others */
    /* Data Structures for Project 2 : User Programs */

		/* Exit code of this process | 
       Will be used when printing the exit message  */
    int exit_code;
    
    /* List of open files with (fd) -> (file *) mapping */
    struct list file_list;
    
    /* Total count of open files
       Will assist in granting fd to a new file */
    int fd_count;
    
    /* List of child process in the form of struct child
       defined in process.h */
    struct list children;
    
    /* Reference to parent thread
       Will be used for passing return status to parent thread */
    struct thread * parent;
    
    /* A flag to reflect to parent process that child process has
       been loaded successfully */
    bool production_flag;
    /* Parent thread needs to sleep until child successfully starts execution 
       production_sem fulfills this purpose */
    struct semaphore production_sem;
    
    /* File pointer to the executable 
       Executable file needs to be closed when exiting */
    struct file * file;
    
    /* Used when a thread waits for a child */
    struct semaphore child_sem;
    /* To keep track of the child a thread has been waiting for */
    tid_t waiton_child;
    
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;
int load_avg;
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
#endif /* threads/thread.h */
