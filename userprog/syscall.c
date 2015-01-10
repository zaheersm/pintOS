#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "process.h"
#include "pagedir.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);
void halt(void);
bool create (const char *, unsigned);
int write(int,const void *, unsigned);
void exit(int);
bool valid (void * vaddr);
void kill (void);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}



static void
syscall_handler (struct intr_frame *f) 
{
  //printf ("In System call handler!\n");
  if(!valid (f->esp))
  {
    kill();
  }

  // Extracting Stack Pointer

  int * p = f->esp;
  //hex_dump(p,p,64,true);
 
  //printf("Esp %p\n",p); 
  // First 4 bytes represent sys
  int syscall_number = *p;
 
  switch (syscall_number)
  {
    case SYS_HALT:
      halt();
      break;

    case SYS_CREATE:
      if(!valid(p+4) || !valid(p+5) || !valid(*(p+4)))
      {
        kill();
      }
      f->eax = create (*(p+4), *(p+5));
      break;
    case SYS_WRITE:
      if (!valid(p+5) || !valid(p+6) || !valid (p+7)|| !valid(*(p+6)))
        kill();
      
      write(*(p+5),*(p+6),*(p+7));
      break;
    case SYS_EXIT:
      if(!valid(p+1))
        kill();

      exit(*(p+1));
      break;
    default:
      printf("No match\n");
  }

  if (thread_tid () == child.id)
    child.flag = 1;
  //thread_exit ();
}

void halt (void)
{
  shutdown_power_off();
}

int write (int fd, const void *buffer, unsigned length)
{
  if (fd == STDOUT_FILENO)
    putbuf(buffer,length);
  
  
  //printf("fd : %d | Buffer : %s | Length : %d\n",fd,buffer,length);
  return 0;
}

void exit (int status)
{
  thread_current()->exit_code = status;
  thread_exit();
}

bool create (const char * file, unsigned initial_size)
{
  if (file == NULL)
    return -1;
  return  filesys_create(file,initial_size);
}

bool valid(void * vaddr)
{
  return (is_user_vaddr(vaddr) && 
    pagedir_get_page(thread_current()->pagedir,vaddr)!=NULL);
}

void kill () 
{
    thread_current()->exit_code = -1;
    thread_exit();
  
}
