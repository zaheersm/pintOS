#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdio.h>
#include "lib/kernel/list.h"
void syscall_init (void);

/* A lock for access to filesys
   Since filesys is not yet concurrent */
struct lock big_lock;

/* A struct to keep file descriptor -> file pointer mapping*/
struct file_desc
{
  struct file * fp;
  int fd;
  struct list_elem elem;
};




#endif /* userprog/syscall.h */
