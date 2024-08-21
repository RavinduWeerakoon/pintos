#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <stdbool.h>
#include "devices/shutdown.h"
#include "devices/input.h"

#define USER_LOWER_BOUND 0x08048000

static void syscall_handler (struct intr_frame *);
//lock to read and write the files.
static struct lock filelock;


//check if the pointer is valid
//use the vaddr and the pagedir
static void
is_valid_ptr (const void *ptr)
{
  //check if the address is a valid user address uf add<PHY_BASE
  if (!(is_user_vaddr (ptr) && ptr > (void *)USER_LOWER_BOUND)) {
    exit (-1);
  }
  //no mapped physical address for the given addresses
  if (pagedir_get_page (thread_current ()->pagedir, ptr) == NULL) {
    exit (-1);
  }
}

//assign for the syscall interrupt
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filelock);
}

//memory allocation is happened after validating the input
static void
syscall_handler (struct intr_frame *f) 
{
  /* get the syscall number from intr_frame */
  is_valid_ptr (f->esp);
  int num = *((int *)(f->esp));
  //use these variables to get the arguements
  uint32_t arg0, arg1, arg2;

  switch (num)
  {
    case SYS_HALT:
      halt ();
      break;

    case SYS_EXIT:
      is_valid_ptr (f->esp + 4);
      arg0 = *(uint32_t *)(f->esp + 4);
      exit ((int)arg0);
      (f->eax) = (int)arg0;
      break;

    case SYS_EXEC:
      is_valid_ptr (f->esp + 4);
      arg0 = *(uint32_t *)(f->esp + 4);
      is_valid_ptr ((void *)arg0);
      (f->eax) = exec ((char *)arg0);
      break;

    case SYS_WAIT:
      is_valid_ptr (f->esp + 4);
      arg0 = *(uint32_t *)(f->esp + 4);
      (f->eax) = wait ((pid_t)arg0);
      break;

    case SYS_CREATE:
      is_valid_ptr (f->esp + 8);
      arg0 = *(uint32_t *)(f->esp + 4);
      arg1 = *(uint32_t *)(f->esp + 8);
      is_valid_ptr ((void *)arg0);
      (f->eax) = create ((char *)arg0, (unsigned)arg1);
      break;

    case SYS_REMOVE:
      is_valid_ptr (f->esp + 4);
      arg0 = *(uint32_t *)(f->esp + 4);
      is_valid_ptr ((void *)arg0);
      (f->eax) = remove ((char *)arg0);
      break;

    case SYS_OPEN:
      is_valid_ptr (f->esp + 4);
      arg0 = *(uint32_t *)(f->esp + 4);
      is_valid_ptr ((void *)arg0);
      (f->eax) = open ((char *)arg0);
      break;

    case SYS_FILESIZE:
      is_valid_ptr (f->esp + 4);
      arg0 = *(uint32_t *)(f->esp + 4);
      (f->eax) = filesize ((int)arg0);
      break;

    case SYS_READ:
      is_valid_ptr (f->esp + 12);
      arg0 = *(uint32_t *)(f->esp + 4);
      arg1 = *(uint32_t *)(f->esp + 8);
      arg2 = *(uint32_t *)(f->esp + 12);
      is_valid_ptr ((void *)arg1);
      (f->eax) = read ((int)arg0, (void *)arg1, (unsigned)arg2);
      break;

    case SYS_WRITE:
      is_valid_ptr (f->esp + 12);
      arg0 = *(uint32_t *)(f->esp + 4);
      arg1 = *(uint32_t *)(f->esp + 8);
      arg2 = *(uint32_t *)(f->esp + 12);
      is_valid_ptr ((void *)arg1);
      (f->eax) = write ((int)arg0, (void *)arg1, (unsigned)arg2);
      break;

    case SYS_SEEK:  
      is_valid_ptr (f->esp + 8);
      arg0 = *(uint32_t *)(f->esp + 4);
      arg1 = *(uint32_t *)(f->esp + 8);
      seek ((int)arg0, (unsigned)arg1);
      break;

    case SYS_TELL:
      is_valid_ptr (f->esp + 4);
      arg0 = *(uint32_t *)(f->esp + 4);
      (f->eax) = tell ((int)arg0);
      break;

    case SYS_CLOSE:
      is_valid_ptr (f->esp + 4);
      arg0 = *(uint32_t *)(f->esp + 4);
      close ((int)arg0);
      break;
  }
}

//implement the halt which power off the system
void
halt (void)
{
  shutdown_power_off ();
}

void
exit (int status)
{
  struct thread *cur = thread_current ();

  /* wait until parent info is set */
  sema_down (&cur->parent_sema);

  //get the stored parent thrad of the courrent thread
  tid_t parent_tid = cur->parent_tid;
  struct thread *parent_t = get_thread_from_tid (parent_tid);
  if (parent_t != NULL) {
    struct child *ch = NULL;
    struct list *parent_child_list = &parent_t->child_list;
    struct list_elem *e;

    //travrese the list and find the current thread
  
    for (e = list_begin (parent_child_list); e != list_end (parent_child_list);
         e = list_next (e)) {
      ch = list_entry (e, struct child, elem);
      if (ch->child_tid == cur->tid)
        break;
    }

    ASSERT (ch->child_tid == cur->tid);

    //set the status and release the lock
    ch->status = status;
    sema_up (&ch->sema);
  }

  thread_exit ();
}

//since excec means create a new process
pid_t
exec (const char *cmd_input)
{
  return process_execute (cmd_input);
}

int
wait (pid_t pid)
{
  return process_wait (pid);
}


bool
remove (const char *file)
{
  if (file == NULL) {
    exit (-1);
  }

  lock_acquire (&filelock);
  bool success = filesys_remove (file);
  lock_release (&filelock);
  return success;
}

bool
create (const char *file, unsigned init_size)
{
  if (file == NULL) {
    exit (-1);
  }

  lock_acquire (&filelock);
  bool success = filesys_create (file, init_size);
  lock_release (&filelock);
  return success;
}

int
open (const char *file)
{
  if (file == NULL) {
    return -1;
  }

  lock_acquire (&filelock);
  struct file *opened_file = filesys_open (file);
  lock_release (&filelock);
  struct thread *t = thread_current ();
  int fd;

  if (opened_file == NULL) {
    return -1;
  }

  /* find empty entry in fd_table */
  for (fd = 2; fd < MAX_FD; fd++) {
    if (t->fd_table[fd] == NULL) break;
  }
  if (fd == MAX_FD) {
    /* fd_table is full */
    return -1;
  }
  else {
    t->fd_table[fd] = opened_file;
    return fd;
  }
}

int
filesize (int fd)
{
  if (fd >= MAX_FD || fd < 2) {
    return 0;
  }

  struct thread *t = thread_current ();
  struct file *opened_file = t->fd_table[fd];
  int length;

  if (opened_file == NULL) {
    return 0;
  }

  lock_acquire (&filelock);
  length = file_length (opened_file);
  lock_release (&filelock);

  return length;
}

int
read (int fd, void *buffer, unsigned size)
{
  if (fd >= MAX_FD || fd < 0) {
    return 0;
  }

  struct file *file;
  struct thread *t = thread_current ();
  unsigned read_cnt = 0;

  lock_acquire (&filelock);
  if (fd == 0) {
    while (read_cnt <= size) {
      /* read key by input_getc() and write it into buffer at appropriate position */
      *(char *)(buffer + read_cnt++) = input_getc ();
    }
    lock_release (&filelock);
    return read_cnt;
  }

  /* get file from fd */
  file = t->fd_table[fd];
  
  if (file == NULL) {
    lock_release (&filelock);
    return 0;
  }

  read_cnt = file_read (file, buffer, size);
  lock_release (&filelock);
  return (int)read_cnt;
}

int
write (int fd, const void *buffer, unsigned size)
{
  if (fd >= MAX_FD || fd < 0) {
    return 0;
  }

  struct file *file;
  struct thread *t = thread_current ();
  int write_cnt = size;
  
  lock_acquire (&filelock);
  if (fd == 1) {
    putbuf (buffer, size);
    lock_release (&filelock);
    return write_cnt;
  }

  /* get file from fd */
  file = t->fd_table[fd];

  if (file == NULL) {
    lock_release (&filelock);
    return 0;
  }

  write_cnt = file_write (file, buffer, size);
  lock_release (&filelock);
  return write_cnt;
}

void
seek (int fd, unsigned position)
{
  if (fd >= MAX_FD || fd < 2) {
    return;
  }

  struct thread *t = thread_current ();
  struct file *opened_file = t->fd_table[fd];
  
  if (opened_file == NULL) {
    return;
  }

  lock_acquire (&filelock);
  file_seek (opened_file, position);
  lock_release (&filelock);
}

unsigned
tell (int fd)
{
  if (fd >= MAX_FD || fd < 2) {
    return 0;
  }

  struct thread *t = thread_current ();
  struct file *opened_file = t->fd_table[fd];
  int next;
  
  if (opened_file == NULL) {
    return 0;
  }

  lock_acquire (&filelock);
  next = file_tell (opened_file);
  lock_release (&filelock);

  return (unsigned) next;
}

void
close (int fd)
{
  if (fd >= MAX_FD || fd < 0) {
    return;
  }

  struct thread *t = thread_current ();
  struct file *opened_file = t->fd_table[fd];  

  if (fd == 0 || fd == 1) {
    return;
  }
  
  if (opened_file == NULL) {
    return;
  }

  lock_acquire (&filelock);
  file_close (opened_file);
  lock_release (&filelock);
  t->fd_table[fd] = NULL;
}