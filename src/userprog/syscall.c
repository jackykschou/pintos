#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "lib/kernel/console.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "userprog/process.h"
#include "filesys/inode.h"

#define READDIR_MAX_LEN 14

 /* Dereference the pointer at ADDRESS + OFFSET. (4 byte address)
 as the type TYPE. */
#define deref_address(ADDRESS, OFFSET, TYPE)                    \
        *(TYPE*)(((uint32_t*)ADDRESS) + OFFSET)

static struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

/* Newly added function declarations. */
static void check_user_program_addresses (void *address);
static void check_file (char *file);
static void check_fd (int fd);
static struct wait_node *search_child_wait_node_list_pid (struct list *child_wait_node_list, pid_t pid);
static void check_stack_argument_addresses (void *start, int arg_count);

/* Initializes syscall handler. */
void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* The system call handler. Checks the validity of the tokenized arguments. */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* Check the validity of the syscall number */
  check_user_program_addresses (f->esp);

  /* Check the validity of arguments, call the system_call. Stores return values in EAX, if any. */
	switch (deref_address (f->esp, 0, uint32_t))
  	{
  		/* No arguments */
  		case SYS_HALT:
  			halt ();
        break;

  		/* One argument system calls*/
  		case SYS_EXIT:
        check_stack_argument_addresses (f->esp, 1);
  			exit (deref_address (f->esp, 1, int));
        break;

  		case SYS_EXEC:
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = exec (deref_address (f->esp, 1, char*));
        break;

  		case SYS_WAIT:
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = wait (deref_address (f->esp, 1, int));
        break;

  		case SYS_REMOVE:
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = remove (deref_address (f->esp, 1, char*));
        break;

  		case SYS_OPEN:
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = open (deref_address (f->esp, 1, char*));
        break;

  		case SYS_FILESIZE:
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = filesize (deref_address (f->esp, 1, int));
        break;

  		case SYS_TELL:
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = tell (deref_address (f->esp, 1, int));
        break;

  		case SYS_CLOSE:
        check_stack_argument_addresses (f->esp, 1);
  			close (deref_address (f->esp, 1, int));
        break;

  	    /* Two arguments system calls*/
  		case SYS_CREATE:
        check_stack_argument_addresses (f->esp, 2);
  			f->eax = create (deref_address (f->esp, 1, char*), deref_address (f->esp, 2, unsigned));
        break;

  		case SYS_SEEK:
        check_stack_argument_addresses (f->esp, 2); 
  			seek (deref_address (f->esp, 1, int), deref_address (f->esp, 2, unsigned));
        break;

        /* Three arguments system calls*/
  		case SYS_READ:
        check_stack_argument_addresses (f->esp, 3);
  			f->eax = read (deref_address (f->esp, 1, int), deref_address (f->esp, 2, void*), deref_address (f->esp, 3, unsigned));
        break;

  		case SYS_WRITE:
        check_stack_argument_addresses (f->esp, 3);
  			f->eax = write (deref_address (f->esp, 1, int), 
        deref_address (f->esp, 2, void*), deref_address (f->esp, 3, unsigned));
        break;
        
        /* Project 4 System Calls*/
      case SYS_CHDIR:
        check_stack_argument_addresses (f->esp, 1);
        f->eax = chdir (deref_address (f->esp, 1, char*));
        break;

      case SYS_MKDIR:
        check_stack_argument_addresses (f->esp, 1);
        f->eax = mkdir (deref_address (f->esp, 1, char*));
        break;

      case SYS_READDIR:
        check_stack_argument_addresses (f->esp, 1);
        f->eax = readdir (deref_address (f->esp, 1, int), deref_address (f->esp, 2, char*));
        break;

      case SYS_ISDIR:
        check_stack_argument_addresses (f->esp, 1);
        f->eax = isdir (deref_address (f->esp, 1, int));
        break;

      case SYS_INUMBER:
        check_stack_argument_addresses (f->esp, 1);
        f->eax = inumber (deref_address (f->esp, 1, int));
        break;


      default:
      	break;
  	}
}

/* Halt system call. */
void
halt (void) 
{
	shutdown_power_off();
	NOT_REACHED ();
}

/* Exit system call. */
void
exit (int status)
{
  /* Assign the exit status to the wait_node. */
  thread_current ()->wait_node->exit_status = status;
  printf ("%s: exit(%d)\n", thread_current ()->name,status);
  thread_exit ();
	NOT_REACHED ();
}

/* TODO - Absolute or Relative Path Name. */
/* exec system call */
pid_t
exec (const char *file)
{
  pid_t pid;
  check_file (file);
  lock_acquire (&filesys_lock);
  pid = process_execute (file);
  lock_release(&filesys_lock);
  /* Block the process while wait for knowing whether file is successfully loaded. */
  sema_down (&(thread_current ()->load_sema));
  if (pid == TID_ERROR || !thread_current ()->load_success)
    return -1;
  return pid;
}

/* wait system call. */
int
wait (pid_t pid)
{
  struct wait_node *child_wait_node;
  int exit_status;

  /* Check if the process with the given pid is if child, if not, return -1. */
  child_wait_node = search_child_wait_node_list_pid (&thread_current ()->child_wait_node_list, pid);
  if (child_wait_node == NULL)
    {
      return -1;
    }
  /* Block the process and wait for the child to be terminated. */
  sema_down (&child_wait_node->wait_sema);
  list_remove (&child_wait_node->elem);
  exit_status = child_wait_node->exit_status;
  free (child_wait_node);
  return exit_status; 
}

/* TODO - Absolute or Relative Path Name. */
/* create system call. */
bool
create (const char *file, unsigned initial_size)
{
  bool result;

  check_file (file);
  lock_acquire (&filesys_lock);
  result = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return result;
}

/* TODO - Absolute or Relative Path Name. */
/* TODO - Delete Empty Directories as well.*/
/* remove system call. */
bool
remove (const char *file)
{
  bool result;

  check_file (file);
  lock_acquire (&filesys_lock);
  result = filesys_remove (file);
  lock_release (&filesys_lock);
  return result;
}

/* TODO - Absolute or Relative Path Name. */
/* TODO - Open directories as well */
/* open system call. */
int
open (const char *file)
{
  int result;

  check_file (file);
  lock_acquire (&filesys_lock);
  /* If the file is successfully opened, assign a file descriptor to the file, otherwise return -1. */
  result = add_file_descriptor (filesys_open (file));
  lock_release (&filesys_lock);
  return result;
}

/* filesize system call. */
int
filesize (int fd) 
{
  int length;
  struct file *file;

  check_fd (fd);
  lock_acquire (&filesys_lock);
  /* Obtain the file structure. */
  file = get_file_struct (fd);
  length = file_length (get_file_struct (fd));
  lock_release (&filesys_lock);
  return length;
}

/* read system call. */
int
read (int fd, void *buffer, unsigned size)
{
  int size_read = 0;
  int i;

  check_file (buffer);
  lock_acquire (&filesys_lock);

  /* Reads STDIN. */
  if (fd == 0)
    {
      for (i = 0; i < size; i++)
      {
        char temp = input_getc();
        *(((char *)buffer) + i) = temp;
      }
      size_read = size;
    }
  /* Tries to read STDOUT, exit. */
  else if (fd == 1)
    {
      lock_release (&filesys_lock);
      exit (-1);
    }
  /* Reads the file at FD. */
  else
    { 
      check_fd (fd);
      struct file *file = get_file_struct(fd);
      size_read = file_read (get_file_struct (fd), buffer, size);
    }
  lock_release (&filesys_lock);
  return size_read;
}

/* write system call. */
int
write (int fd, const void *buffer, unsigned size)
{   
  int size_written = 0;
  struct file *file;

  check_file (buffer);
  lock_acquire (&filesys_lock);
  /* Tries to write to STDIN, exits. */
  if (fd == 0)
    {
      lock_release (&filesys_lock);
      exit(-1);
    }
  /* Writes to STDOUT. */ 
  else if (fd == 1)
    {
      putbuf (buffer, size);
      size_written = size;  //Entire buffer written to console
    }
  /* Writes to the file at FD. */
  else
    { 
      check_fd (fd);
      file = get_file_struct (fd);
      if (isdir(fd))
        {
          size_written = -1;
        }
      else
        {
          size_written = file_write (file, buffer, size); 
        }
    }
  lock_release (&filesys_lock);
  return size_written;
}

/* seek system call. */
void
seek (int fd, unsigned position) 
{
  struct file *file;

  check_fd (fd);
  lock_acquire (&filesys_lock);
  /* Obtain the file structure. */
  file = get_file_struct (fd);
  file_seek (file, position);
  lock_release (&filesys_lock);
}

/* tell system call. */
unsigned
tell (int fd) 
{
  struct file *file;

  check_fd (fd);
  /* Obtain the file structure. */
  file = get_file_struct (fd);
  return file_tell (file);
}

/* close system call. */
void
close (int fd)
{
  struct file *file;

  check_fd (fd);
  lock_acquire (&filesys_lock);
  file = get_file_struct (fd);
  //here
  file_close (file);
  remove_file_descriptor (fd);
  lock_release (&filesys_lock);
}

/* START TODO */

/* System Calls for Project 4 */

/* Changes current working directory. True if successful. */
bool 
chdir (const char *dir)
{
  lock_acquire (&filesys_lock);
  bool result;
  result = filesys_chdir (dir);
  lock_release (&filesys_lock);
  return result;
}

/* Creates directory. True if successful. */
bool 
mkdir (const char *dir)
{
  lock_acquire (&filesys_lock);
  bool result = filesys_mkdir(dir);
  lock_release (&filesys_lock);
  return result;
}

/* Reads a directory entry from a directory represented by fd.
   If successful, stores name and returns true. */
bool 
readdir (int fd, char *name)
{
  lock_acquire (&filesys_lock);
  struct file *myfile;
  myfile = get_file_struct (fd);
  /* If the file is not a directory, return false. */
  if (!myfile->inode->data.is_dir)
    {
      lock_release (&filesys_lock);
      return false;
    }
  struct dir *dir = dir_open (myfile->inode);
  /* Fails to open directory, return false. */
  if (dir == NULL)
    {
      lock_release (&filesys_lock);
      return false;
    }

  bool still_have_thing = true;
  bool result = false;
  char name_buffer[READDIR_MAX_LEN + 1];
  /* Ignore "." and "..". */
  while (still_have_thing)
    {
      if (!dir_readdir (dir, name_buffer))
        {
          still_have_thing = false;
        }
      else if (strcmp (name_buffer, ".") && strcmp (name_buffer, ".."))
        {
          result = true;
          strlcpy (name, name_buffer, strlen (name_buffer) + 1);
        }
    }
  dir_close (dir);
  lock_release (&filesys_lock);
  return result;
}

/* True if fd represents a directory.*/
bool 
isdir (int fd)
{
  check_fd (fd);
  //fd must represent a directory.
  //false if ordinary file.
  struct file *myfile;
  myfile = get_file_struct (fd);

  struct inode* myinode;

  return myfile->inode->data.is_dir;
}

/* Returns inode number of inode associated with fd. */
int 
inumber (int fd)
{
  lock_acquire (&filesys_lock);
  struct file *myfile;
  myfile = get_file_struct (fd);
  lock_release (&filesys_lock);

  return myfile->inode->sector;
}

/* END TODO */

/* Checks the validity of a user process address. */ 
static void
check_user_program_addresses (void *address)
{
  if (address == NULL || !is_user_vaddr (address) || pagedir_get_page (thread_current ()->pagedir, address) == NULL)
    exit (-1);  
}

/* Given the stack pointer, check the validity of the given number of arguments (32-bit addresses) following it */
static void
check_stack_argument_addresses (void *start, int arg_count)
{
  int i;
  for (i = 1; i <= arg_count; i++)
    {
      check_user_program_addresses (start + (i * sizeof (int)));
    }
}

/* Checks the validity of the address of a file name. */
static void
check_file (char *file)
{
  if (file == NULL || !is_user_vaddr (file) || pagedir_get_page (thread_current ()->pagedir, file) == NULL)
    exit (-1);
}

/* Checks the validity of a file descriptor. */
static void
check_fd (int fd)
{
  if (fd < 0 || fd >= MAX_OPEN_FILES || get_file_struct (fd) == NULL)
    exit (-1);
}

/* Find the wait_node of a process given its pid, return NULL is not found */
static struct wait_node *
search_child_wait_node_list_pid (struct list *child_wait_node_list, pid_t pid)
{
  struct list_elem *e;
  struct wait_node *n;

  for (e = list_begin (child_wait_node_list); e != list_end (child_wait_node_list);
       e = list_next (e))
    {
    n = list_entry (e, struct wait_node, elem);
    if (n->pid == pid)
      {
        return n;
      }
    }

  return NULL;
}
