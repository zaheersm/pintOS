#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdio.h>
#include "lib/kernel/list.h"
void syscall_init (void);

struct lock big_lock;
struct file_desc
{
  struct file * fp;
  int fd;
  struct list_elem elem;
};




#endif /* userprog/syscall.h */
