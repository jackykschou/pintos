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
#include "userprog/process.h"

#define deref_address(ADDRESS, OFFSET, TYPE)                    \
        *(TYPE*)(((uint32_t*)ADDRESS) + OFFSET)

static void syscall_handler (struct intr_frame *);
static void check_user_program_addresses (void *address);
static struct thread* search_child_list_pid (struct list *child_list, pid_t pid);
static void check_file (char *file);
static void check_fd (int fd);

struct lock filesys_lock;

void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	//printf ("system call!\n");

  // printf("hex %02hhx\n",f->esp);
  // printf("hex star %02hhx\n", *(uint32_t*)(f->esp));

  /* Check the validity of the syscall number */
  check_user_program_addresses (f->esp);
  
	switch (deref_address (f->esp, 0, uint32_t))
  	{
  		/* No arguments */
  		case SYS_HALT:
        //printf("0\n");
  			halt ();
        break;

  		/* One argument */
  		case SYS_EXIT:
        // printf("exit\n");
        //We are checking the addresses of arguments for validity
        check_stack_argument_addresses (f->esp, 1);
        //printf("checked2\n");
  			exit (deref_address (f->esp, 1, int));
        break;

  		case SYS_EXEC:
        // printf("2\n");
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = exec (deref_address (f->esp, 1, char*));
        break;

  		case SYS_WAIT:
        // printf("3\n");
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = wait (deref_address (f->esp, 1, int));
        break;

  		case SYS_REMOVE:
        // printf("4\n");
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = remove (deref_address (f->esp, 1, char*));
        break;

  		case SYS_OPEN:
        // printf("5\n");
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = open (deref_address (f->esp, 1, char*));
        break;

  		case SYS_FILESIZE:
        // printf("6\n");
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = filesize (deref_address (f->esp, 1, int));
        break;

  		case SYS_TELL:
        // printf("7\n");
        check_stack_argument_addresses (f->esp, 1);
  			f->eax = tell (deref_address (f->esp, 1, int));
        break;

  		case SYS_CLOSE:
        // printf("8\n");
        check_stack_argument_addresses (f->esp, 1);
  			close (deref_address (f->esp, 1, int));
        break;

  	    /* Two arguments */
  		case SYS_CREATE:
        // printf("9\n");
        check_stack_argument_addresses (f->esp, 2);
  			f->eax = create (deref_address (f->esp, 1, char*), deref_address (f->esp, 2, unsigned));
        break;


  		case SYS_SEEK:
        // printf("10\n");
        check_stack_argument_addresses (f->esp, 2); 
  			seek (deref_address (f->esp, 1, int), deref_address (f->esp, 2, unsigned));
        break;

        /* Three arguments */
  		case SYS_READ:
        // printf("11\n");
        check_stack_argument_addresses (f->esp, 3);
  			f->eax = read (deref_address (f->esp, 1, int), deref_address (f->esp, 2, void*), deref_address (f->esp, 3, unsigned));
        break;

  		case SYS_WRITE:
        // printf("12\n");
        check_stack_argument_addresses (f->esp, 3);
  			f->eax = write (deref_address (f->esp, 1, int), 
        deref_address (f->esp, 2, void*), deref_address (f->esp, 3, unsigned));
        break;

      default:
      	break;
  	}
}

void
halt (void) 
{
	shutdown_power_off();
	NOT_REACHED ();
}

void
exit (int status)
{
  if (thread_current ()->parent_thread->child_waiting_for == thread_current ())
    thread_current ()->parent_thread->child_exit_status = status;
  printf ("%s: exit(%d)\n", thread_current ()->name,status);  //need name of process
  thread_exit ();
	NOT_REACHED ();
}

pid_t
exec (const char *file)
{
  check_file (file);
  lock_acquire (&filesys_lock);
  tid_t tid = process_execute (file);
  lock_release(&filesys_lock);
  
  if (tid == TID_ERROR)
    return -1;
  else
    return tid;
}

int
wait (pid_t pid)
{
  struct thread *child_thread = search_child_list_pid (&thread_current ()->child_list, pid);
  if (child_thread == NULL)
    {
      return -1;
    }
  else
    {
      // printf("lalalalal I am waiting\n");
      thread_current ()->child_waiting_for = child_thread;
      sema_down (&thread_current ()->wait_sema);
      return thread_current ()->child_exit_status;
    } 
}

static struct thread*
search_child_list_pid (struct list *child_list, pid_t pid)
{
  struct list_elem *e;

  for (e = list_begin (child_list); e != list_end (child_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, child_elem);
      if (t->pid == pid)
        {
          return t;
        }
    }
    return NULL;
}

bool
create (const char *file, unsigned initial_size)
{
  check_file (file);
  lock_acquire(&filesys_lock);
  bool result = filesys_create (file, initial_size);
  lock_release(&filesys_lock);
  return result;
}

bool
remove (const char *file)
{
  check_file (file);
  lock_acquire (&filesys_lock);
  bool result = filesys_remove(file);
  lock_release(&filesys_lock);
  return result;
}

int
open (const char *file)
{
  check_file (file);
  lock_acquire(&filesys_lock);
  int result = add_file_descriptor (filesys_open(file));
  lock_release(&filesys_lock);
  return result;
}

int
filesize (int fd) 
{
  check_fd (fd);
  lock_acquire (&filesys_lock);
  struct file *file = get_file_struct(fd);
  off_t length = file_length (get_file_struct (fd));
  lock_release (&filesys_lock);
  return length;
}

int
read (int fd, void *buffer, unsigned size)
{
  //check good
  check_fd (fd);
  check_file(buffer);

  lock_acquire(&filesys_lock);
  int size_read = 0;

  if (fd == 0)
      {
        int i;
        for(i = 0; i < size; i++)
        {
          char temp = input_getc();
          *(((char*)buffer)+ i) = temp;
        }
        size_read = size;
      }
  else if (fd == 1)
      {
         exit(-1);
      }
  else
      { 
        check_fd(fd);
        struct file *file = get_file_struct(fd);
        size_read = file_read (get_file_struct (fd), buffer, size);
      }

  

  lock_release(&filesys_lock);
  return size_read;
}

/* Writes to open file or console. Returns the size of what was written. 
   Argument size may not equal the size written if space is limited. */
int
write (int fd, const void *buffer, unsigned size)
{   
  check_file(buffer);

  lock_acquire(&filesys_lock);
  int size_written = 0;  //Nothing written

    if (fd == 0)
      {
        exit(-1);
      }
    else if (fd == 1)
      {
        putbuf (buffer, size);
        size_written = size;  //Entire buffer written to console
      }
    else
      { 
        check_fd(fd);
        struct file *file = get_file_struct(fd);
        size_written = file_write(file, buffer, size); 
      }

  lock_release(&filesys_lock);
  return size_written;
}

void
seek (int fd, unsigned position) 
{
  check_fd (fd);
  lock_acquire(&filesys_lock);
  struct file *file = get_file_struct(fd);
  file_seek (file, position);
  lock_release(&filesys_lock);
}

unsigned
tell (int fd) 
{
  check_fd (fd);
  struct file *file = get_file_struct(fd);
  return file_tell (file);
}

void
close (int fd)
{
  check_fd (fd);
  lock_acquire(&filesys_lock);
  struct file *file = get_file_struct(fd);
  file_close (file);
  remove_file_descriptor (fd);
  lock_release(&filesys_lock);
}

/* Checks the validity of a user address */ 
static void
check_user_program_addresses (void *address)
{
  if (address == NULL || !is_user_vaddr(address) || pagedir_get_page (thread_current ()->pagedir, address) == NULL)
          exit (-1);  
}

/* Given the stack pointer, check the validity of the given number of arguments (32-bit addresses) following it */
check_stack_argument_addresses(void *start, int arg_count)
{
  int i;
  for(i=1; i<=arg_count; i++)
  {
    check_user_program_addresses(start + (i * sizeof(int)));
  }
}

static void
check_file (char *file)
{
  if (file == NULL || !is_user_vaddr(file) || pagedir_get_page (thread_current ()->pagedir, file) == NULL)
    exit (-1);
}

static void
check_fd (int fd)
{
  if (fd < 0 || fd >= MAX_OPEN_FILES || get_file_struct (fd) == NULL)
    exit (-1);
}