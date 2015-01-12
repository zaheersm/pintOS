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

/* System Calls */
void halt(void);
void exit(int);
tid_t exec (const char * cmd_line);
int wait(tid_t);
bool create (const char *, unsigned);
bool remove(const char *);
int open (const char * file);
int filesize (int fd);
int read (int, void *, unsigned);
int write (int,const void *, unsigned);
void seek(int,unsigned);
unsigned tell (int);
void close (int fd);

/* Helper Functions */

/* Checks if the user address is valid */
bool valid (void * vaddr);
/* Calls exit with -1 status */
void kill (void);
/* Returns file * equivalent to file descriptor */
struct file_desc * get_file(int);

void
syscall_init (void) 
{
  /* Initializing big lock i.e. lock for filesys */
  lock_init(&big_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  // Extrating Stack pointer
  int * p = f->esp;
  
  /* If stack pointer is not valid, kill */
  if(!valid (p))
    kill();
  
  //hex_dump(p,p,64,true);
 
  int syscall_number = *p;
  
  /* 
  
  There's a discrepancy in stack frame 
  Sys call with 1 arg has its arg at esp+1
  Sys call with 2 args has its args at esp+4 and esp+5
  Sys call with 3 args has its args at esp+5, esp+6 and esp+7
  
  Before executing a sys call functionality,
  args are validated i.e. they are at a valid user memory location
  If an arg is a pointer (eg. a string), the memory location
  it points to is checked seperately
  
  If an arg fails to get validated, the process is killed

  If a sys call has return value, we put it up in stack's
  frame eax register
  */
  

  switch (syscall_number)
  {
    case SYS_HALT:
      halt();
      break;
		
		case SYS_EXIT:
      if(!valid(p+1))
        kill();
			exit(*(p+1));
      break;

    case SYS_EXEC:
      if(!valid(p+1) || !valid(*(p+1)))
        kill();
      f->eax = exec(*(p+1));
      break;
    
		case SYS_WAIT:
      if(!valid(p+1))
        kill(); 
      f->eax = wait(*(p+1));
      break;
    
    case SYS_CREATE:
      if(!valid(p+4) || !valid(p+5) || !valid(*(p+4)))
        kill();
      f->eax = create (*(p+4), *(p+5));
      break;
    
    case SYS_REMOVE:
      if(!valid(p+1) || !valid(*(p+1)))
        kill();
      f->eax = remove(*(p+1));
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
		
		case SYS_WRITE:
      if (!valid(p+5) || !valid(p+6) || 
            !valid (p+7)|| !valid(*(p+6)))
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
		
		case SYS_CLOSE:
      if (!valid(p+1))
        kill();
      close(*(p+1));
      break;
    
		default:
      /* Invalid System Call Number | we kill the process */
      hex_dump(p,p,64,true);
      printf("Invalid System Call number\n");
			kill();      
			break;
  }

}

/* Shutdown Pintos */
void halt (void)
{
  shutdown_power_off();
}

void exit (int status)
{
  struct thread * parent = thread_current()->parent;
  
  /* Updating the exit code of current thread*/
  thread_current()->exit_code = status;
  if (!list_empty(&parent->children))
  {
    
    /* 
    If the thread to be exited is a child of another thread,
    we update the ret_val in the parent's relevant struct
    i.e. children list's element for the exiting thread

    We wake up the parent thread if it is waiting for this thread's
    completion
    */

    /* get_child returns parents relevant struct */
    struct child * child = get_child(thread_current()->tid,parent);

    if (child!=NULL)
    {
      child->ret_val=status;
      child->used = 1;
      
      /* 
        Waking up the parent thread if it is waiting 
        on the current thread 
      */
      if (thread_current()->parent->waiton_child 
              == thread_current()->tid)
        sema_up(&thread_current()->parent->child_sem);
        
    }
  }
  
  thread_exit();
}




tid_t exec (const char * cmd_line)
{
  /* 
  Acquiring filesys lock i.e. big lock
  */
  lock_acquire(&big_lock);
  tid_t tid = process_execute(cmd_line);
  lock_release(&big_lock);
  return tid;
}
int wait(tid_t id)
{
  /* 
  Waiting for 'id' only if it is calling thread's child
  */
  tid_t tid = process_wait(id);
  return tid;
}

/* Creates a new file | doesn't open it */
bool create (const char * file, unsigned initial_size)
{
  if (file == NULL)
    return -1;
  
  lock_acquire(&big_lock);
  int ret = filesys_create(file,initial_size);
  lock_release(&big_lock);
  
  return ret;
}

/* Removes a file | doesn't close it */
bool remove (const char * file)
{

  if (file == NULL)
    return -1;
  lock_acquire(&big_lock);
  bool flag = filesys_remove(file);
  lock_release(&big_lock);

  return flag;
}

/* 
Opens a file, add it to the current thread's
file list and returns the descriptor 
*/
int open (const char * file)
{
  lock_acquire(&big_lock);
  struct file * fp = filesys_open (file);
  lock_release(&big_lock);
  
  if (fp == NULL) 
    return -1;
  
  struct file_desc * fd_elem = malloc (sizeof(struct file_desc));
  fd_elem->fd = ++thread_current()->fd_count;
  fd_elem->fp = fp;
  list_push_front(&thread_current()->file_list,&fd_elem->elem);
  
  return fd_elem->fd;
}

/* Returns the size in bytes of the file open as fd */
int filesize (int fd)
{
  /* 
  Get file function returns the elem of the file list
  having the descriptor fd 
  */
  struct file_desc * fd_elem = get_file(fd);
  
  /* If no elem having the descriptor fd exists */
  if(fd_elem == NULL)
    return -1;
      
  lock_acquire(&big_lock);
  /* 
  Retrieving length/size using filesys function
  for equivalent file pointer 
  */
  int length = file_length(fd_elem->fp); 
  lock_release(&big_lock);
  return length;
}

/* Reads length bytes from the open file as fd into buffer */
int read (int fd, void * buffer, unsigned length)
{
  int len =0;
  
  // If fd == 0, reads from keyboard using input_getc()
  if (fd == STDIN_FILENO)
  { 
    while (len < length)
    {
      *((char *)buffer+len) = input_getc();
      len++;
    }
    return len;
  }

  // For an fd other than 0, retrieve file_desc elem
  struct file_desc * fd_elem = get_file(fd);
  if (fd_elem == NULL)
    return -1;
  
  lock_acquire(&big_lock);
  // Read from the file using filesys function
  len = file_read(fd_elem->fp,buffer,length);
  lock_release(&big_lock);
  return len;
}

// Writes size bytes from buffer to the open file fd
int write (int fd, const void *buffer, unsigned length)
{
  // if fd == 1, write to standard output
  if (fd == STDOUT_FILENO)
  {
    putbuf(buffer,length);
    return length;
  }
  
  // For fd other than 1, retrieve file_desc elem
  struct file_desc * fd_elem = get_file(fd);
  if(fd_elem == NULL)
    return -1;
  
  lock_acquire(&big_lock);
  // write to the file using filesys function
  int ret = file_write(fd_elem->fp,buffer,length);
  lock_release(&big_lock);
  return ret;
}

/*  
Changes the next byte to be read or written
in open file fd to position 
*/
void seek (int fd, unsigned position)
{
  struct file_desc * fd_elem = get_file(fd);

  if (fd_elem == NULL)
    return;
  
  lock_acquire(&big_lock);
  file_seek(fd_elem->fp,position);
  lock_release(&big_lock);
}

/*
Returns the position of the the next byte
to be read or written in open file fd
*/
unsigned tell (int fd)
{
  struct file_desc * fd_elem = get_file(fd);

  if (fd_elem == NULL)
    return -1;
  
  lock_acquire(&big_lock);
  unsigned pos = file_tell (fd_elem->fp);
  lock_release(&big_lock);
  return pos;
}

/* Closes the file descriptor fd */
void close (int fd)
{
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
    return;
  
  // Retrieving file_desc element equivalent to fd
  struct file_desc * fd_elem = get_file(fd);

  if (fd_elem == NULL)
    return -1;
  
  lock_acquire(&big_lock);
  // Closing file using file sys function
  file_close(fd_elem->fp);
  lock_release(&big_lock);

  /* Removing the file desc element from the list
     and freeing memory */
  list_remove(&fd_elem->elem);
  free(fd_elem);

}

/* Checks for validity of a user address
   It should be below PHYS_BASE and 
   registered in page directory */
bool valid(void * vaddr)
{
  return (is_user_vaddr(vaddr) && 
    pagedir_get_page(thread_current()->pagedir,vaddr)!=NULL);
}

/* Exits the process with -1 status */
void kill () 
{
  exit(-1);
  
}

/* Simple function to traverse current threads
  file list and return file_desc element's pointer
  equivalent to fd */
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
