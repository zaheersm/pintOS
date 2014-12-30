/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"


/*  Comparator function for waiters list in a semaphore */
bool less_waiters (const struct list_elem *, const struct list_elem*,void * aux);


/*  Comparator function for waiters list in a sempahore_elem
    used in condition variable */
bool less_cond_waiters(const struct list_elem *, const struct list_elem * , void * aux);

/* Donates priority when acquiring lock */
void donate_priority (struct lock *);

/* Releases priority when releasing lock */
void release_priority (struct lock *);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      /* Insert in order in sema waiters list w.r.t priority */
      list_insert_ordered(&sema->waiters,&thread_current()->elem,less_waiters,NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Comparator function for inserting thread in-order in sema->waiters list */
bool less_waiters (const struct list_elem * a, const struct list_elem *b, void * aux)
{
  struct thread * t_a = list_entry(a, struct thread, elem);
  struct thread * t_b = list_entry(b, struct thread, elem);

  if(t_a -> priority < t_b->priority)
    return true;
  else
    return false;
}


/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  
  old_level = intr_disable ();
  struct thread * t = NULL;
  if (!list_empty (&sema->waiters))
  {
    /* Sort the sema->waiters list because priority donation 
      could have altered the priority under the hood */
    list_sort(&sema->waiters, less_waiters,NULL);
    /* Pops off the highest priority thread from sema->waiters list */
    t = list_entry(list_pop_back(&sema->waiters),struct thread, elem);
    thread_unblock(t);
  }
  sema->value++;
  intr_set_level (old_level);
  
  /* If the currently unblocked thread has higher priority then
    current thread, yield */
  old_level = intr_disable();
  
  if (t!= NULL)
  {
    if (thread_current()->priority < t->priority)
      thread_yield();
  }
  intr_set_level(old_level);

}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  

  enum intr_level old_level;

  old_level = intr_disable();
  /* If the lock is not available, donate priority */
  if(lock->holder != NULL)
  {
    donate_priority(lock);
  }
  sema_down (&lock->semaphore);
  /* Here the lock has been acquired so set the wait_lock to NULL*/
  thread_current()->wait_lock = NULL;
  
  /* Set the lock holder to current thread */
  lock->holder = thread_current ();
  
  intr_set_level(old_level);
}

void
donate_priority(struct lock* lock)
{
  //TODO: Assert that interrupts are off
  //TODO: Assert lock->holder != NULL
  
  /* Set the wait_lock of current thread to the lock
    that this thread wants to acquire */
  thread_current()->wait_lock = lock;
 
  /* Add this thread to the donors list of the holder's thread */
  list_push_back(&lock->holder->donors_list, &thread_current()->donor_elem);
  
  // Variables used for iteration
  struct thread * t_curr = thread_current();
  struct lock * l_curr = lock;
  
  // Variable to keep track of donation depth
  int i = 0;
  
  while (l_curr!=NULL && i != 8)
  { 
    if(t_curr->priority <= l_curr->holder->priority)
      return;
    // When priority is updated, thread should be re-inserted in sema_waiters list
    // Shortcut: In sema_up, sort sema_waiters and then pop (implemented currently)
    l_curr->holder->priority = t_curr->priority;
    
    // Updating iteration variables
    t_curr = l_curr->holder;
    l_curr = l_curr->holder->wait_lock;
    i++;
  }
  
  return;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  
  enum intr_level old_level;
  old_level = intr_disable();

  lock->holder = NULL;
  release_priority(lock);
  sema_up (&lock->semaphore);

  intr_set_level(old_level);

}

void release_priority (struct lock * lock) 
{
  /* Empty donors list implies that nobody has donated
    priority to this thread, hence return */
  if (list_empty(&thread_current()->donors_list))
    return;
  
  /* Traverse the donors list and remove entries which were waiting on the
  lock currently being release */
  struct thread * curr = thread_current();
  struct list_elem * e;

  for (e = list_begin(&curr->donors_list); e!= list_end(&curr->donors_list); e=list_next(e))
  {
    struct thread * t = list_entry ( e, struct thread, donor_elem);
    if (t->wait_lock == lock)
      list_remove(e);
  }
  
  /* If donors list is empty, restore original priority */
  if (list_empty(&curr->donors_list))
  {
    curr->priority = curr->original_priority;
  }
  /* Otherwise set the priority to the maximum priority of those
    left in the donors list */
  else
  {
    int max = curr->original_priority;
    for (e = list_begin(&curr->donors_list); e!=list_end(&curr->donors_list); e= list_next(e))
    {
      struct thread * t = list_entry (e, struct thread, donor_elem);
      if(t->priority > max)
        max = t->priority;
    }
    curr->priority = max;  
  }
    
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    int priority;
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  waiter.priority = thread_current()->priority;
  list_insert_ordered(&cond->waiters,&waiter.elem, less_cond_waiters,NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* Comparator function to insert in order in cond->waiters list */
bool less_cond_waiters (const struct list_elem * a, const struct list_elem * b, void * AUX)
{
  struct semaphore_elem * s_a = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem * s_b = list_entry(b, struct semaphore_elem, elem);

  if(s_a->priority < s_b->priority)
    return true;
  else
    return false;

}
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  /* Signal highest priority thread */
  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_back(&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
