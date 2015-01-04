#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "process.h"
#include "threads/vaddr.h"
static void syscall_handler (struct intr_frame *);

void extractWriteArguments(void * buffer, void * esp);
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  
  printf ("In System call handler!\n");
 
 // Extracting Stack Pointer
  int * p = f->esp;
  hex_dump(p,p,64,true);
  
  printf("Esp %p\n",p); 
  // First 4 bytes represent sys
  int syscall_number = *p;
 
  char * buffer;
  int fd;
  unsigned length;
  switch (syscall_number)
  {
    case SYS_WRITE:
      printf("fd : %d | Length : %d\n",*(p+1),*(p+3));
      printf("buffer: %s\n",*(p+4)); 
      
      break;
    default:
      printf("No match\n");
  }

  if (thread_tid () == child.id)
    child.flag = 1;
  thread_exit ();
}

int write (int fd, const void *buffer, unsigned length)
{
  return 0;
}
/*
void extractWriteArguments(void ** buffer, void * esp)
{
      
  


}*/
