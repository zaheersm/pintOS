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
//#include "filesys/inode.c"
struct file_desc;

static void syscall_handler (struct intr_frame *);
void halt(void);
bool create (const char *, unsigned);
int open (const char * file);
int read (int, void *, unsigned);
int write (int,const void *, unsigned);
void seek(int,unsigned);
unsigned tell (int);
void close (int fd);
int filesize (int fd);
void exit(int);
bool valid (void * vaddr);
void kill (void);
struct file_desc * get_file(int);

struct file_desc
{
  struct file * fp;
  int fd;
  struct list_elem elem;
};

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
    
    case SYS_OPEN:
      if(!valid (p+1) || !valid(*(p+1)))
        kill();

      f->eax = open (*(p+1));
    
      break;
   
    case SYS_FILESIZE:
      if (!valid(p+1))
        kill();
      f->eax = filesize(*(p+1));
      break;

    case SYS_READ:
      if (!valid(p+5) || !valid (p+6) || !valid (p+7) || !valid (*(p+6)))
        kill();
      f->eax=read(*(p+5),*(p+6),*(p+7));
      break;
    
    case SYS_CLOSE:
      if (!valid(p+1))
        kill();
      close(*(p+1));
      break;
    
    case SYS_WRITE:
      if (!valid(p+5) || !valid(p+6) || !valid (p+7)|| !valid(*(p+6)))
        kill();
      f->eax = write(*(p+5),*(p+6),*(p+7));
      break;
    
    case SYS_SEEK:
      if(!valid(p+4) || !valid(p+5))
        kill();

      seek(*(p+4),*(p+5));
      break;
    
    case SYS_TELL:
      if(!valid(p+1))
        kill();
      
      f->eax = tell(*(p+1));
      break;
    
    case SYS_EXIT:
      if(!valid(p+1))
        kill();

      exit(*(p+1));
      break;
    default:
      printf("No match\n");
  }

  //thread_exit ();
}

void halt (void)
{
  shutdown_power_off();
}

int write (int fd, const void *buffer, unsigned length)
{
  if (fd == STDOUT_FILENO)
  {
    putbuf(buffer,length);
    return length;
  }

  struct file_desc * fd_elem = get_file(fd);
  if(fd_elem == NULL)
    return -1;

  return file_write(fd_elem->fp,buffer,length);
}

void exit (int status)
{
  struct thread * parent = thread_current()->parent;

  if (!list_empty(&parent->children))
  {
    struct child * child = get_child(thread_current()->tid,parent);

    if (child!=NULL)
    {
      child->ret_val=status;
      sema_up(&child->sem);
    }
  }
  thread_current()->exit_code = status;
  thread_exit();
}

bool create (const char * file, unsigned initial_size)
{
  if (file == NULL)
    return -1;
  return  filesys_create(file,initial_size);
}

int open (const char * file)
{
  struct file * fp = filesys_open (file);
  
  if (fp == NULL)
  { 
    return -1;
  }
  struct file_desc * fd_elem = malloc (sizeof(struct file_desc));
  fd_elem->fd = ++thread_current()->fd_count;
  fd_elem->fp = fp;
  list_push_front(&thread_current()->file_list,&fd_elem->elem);
  
  return fd_elem->fd;
}

int filesize (int fd)
{
  struct file_desc * fd_elem = get_file(fd);
  if(fd_elem == NULL)
    return -1;
  
  return file_length(fd_elem->fp);
}

int read (int fd, void * buffer, unsigned length)
{
  int i =0;
  
  if (fd == STDIN_FILENO)
  {
    while (i < length)
    {
      *((char *)buffer+i) = input_getc();
      i++;
    }
    return i;
  }

  struct file_desc * fd_elem = get_file(fd);
  if (fd_elem == NULL)
    return -1;
  return file_read (fd_elem->fp, buffer, length);
}

void seek (int fd, unsigned position)
{
  struct file_desc * fd_elem = get_file(fd);

  if (fd_elem == NULL)
    return;

  file_seek(fd_elem->fp,position);
}

unsigned tell (int fd)
{
  struct file_desc * fd_elem = get_file(fd);

  if (fd_elem == NULL)
    return -1;

  return file_tell(fd_elem->fp);
}

void close (int fd)
{
  struct thread * curr = thread_current();
  struct list_elem * e;

  for ( e = list_begin(&curr->file_list);
    e != list_end (&curr->file_list); e = list_next(e))
  {
    struct file_desc * fd_elem = list_entry(e,struct file_desc,elem);

    if (fd_elem->fd == fd)
    {
      file_close(fd_elem->fp);
      list_remove(&fd_elem->elem);
      free(fd_elem);
      break;
    }
  }

}

bool valid(void * vaddr)
{
  return (is_user_vaddr(vaddr) && 
    pagedir_get_page(thread_current()->pagedir,vaddr)!=NULL);
}

void kill () 
{
    struct thread * parent = thread_current()->parent;

    if(!list_empty(&parent->children))
    {
      struct child * child = get_child(thread_current()->tid,parent);

      if(child!= NULL)
      {
        child->ret_val = -1;
        sema_up(&child->sem); 
      }
    }
  
    thread_current()->exit_code = -1;
    thread_exit();
}

struct file_desc * get_file (int fd)
{
  struct thread * curr = thread_current();
  struct list_elem * e;

  for (e=list_begin(&curr->file_list);
    e != list_end (&curr->file_list); e = list_next(e))
  {
    struct file_desc * fd_elem = list_entry(e, struct file_desc,elem);
    if (fd_elem->fd == fd)
      return fd_elem;
  } 

  return NULL;
}
