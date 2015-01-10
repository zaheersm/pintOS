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
  void * esp = f->esp;
  hex_dump(esp,esp,20,true);
  
  printf("Esp %p\n",esp); 
  // First 4 bytes represent sys
  int syscall_number = *((int *) esp);
  esp+= 4;


  
  
  //printf("Stack pointer %x\n",esp);
  char * buffer;
  int fd;
  unsigned length;
  switch (syscall_number)
  {
    case SYS_WRITE:

      fd = *((int *)esp);
      esp+=4;

      buffer = (char *)esp;
      //buffer = user_to_kernel_ptr((const void *) buffer);
      esp+=4;
      length = *((unsigned *)esp);
      esp+=4;
      printf("Boy want's to write\n");
      printf("Extracted Arguments:\n");
      printf("Buffer str: %s\n", buffer);
      printf("fd : %d\nbuffer: %p\nlength: %d\n",fd,(void *)buffer,length);
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
