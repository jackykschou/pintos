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

	switch (deref_address (f->esp, 0, uint32_t))
  	{
  		/* No arguments */
  		case SYS_HALT:
        printf("0\n");
  			halt ();
        break;

  		/* One argument */
  		case SYS_EXIT:
        printf("1\n");
  			exit (deref_address (f->esp, 1, int));
        break;

  		case SYS_EXEC:
        printf("2\n");
  			f->eax = exec (deref_address (f->esp, 1, char*));
        break;

  		case SYS_WAIT:
        printf("3\n");
  			f->eax = wait (deref_address (f->esp, 1, int));
        break;

  		case SYS_REMOVE:
        printf("4\n");
  			f->eax = remove (deref_address (f->esp, 1, char*));
        break;

  		case SYS_OPEN:
        printf("5\n");
  			f->eax = open (deref_address (f->esp, 1, char*));
        break;

  		case SYS_FILESIZE:
        printf("6\n");
  			f->eax = filesize (deref_address (f->esp, 1, int));
        break;

  		case SYS_TELL:
        printf("7\n");
  			f->eax = tell (deref_address (f->esp, 1, int));
        break;

  		case SYS_CLOSE:
        printf("8\n");
  			close (deref_address (f->esp, 1, int));
        break;

  	    /* Two arguments */
  		case SYS_CREATE:
        printf("9\n");
  			f->eax = create (deref_address (f->esp, 1, char*), deref_address (f->esp, 2, unsigned));
        break;


  		case SYS_SEEK:
        printf("10\n");
  			seek (deref_address (f->esp, 1, int), deref_address (f->esp, 2, unsigned));
        break;

        /* Three arguments */
  		case SYS_READ:
        printf("11\n");
        check_user_program_addresses (deref_address (f->esp, 2, void*));
  			f->eax = read (deref_address (f->esp, 1, int), deref_address (f->esp, 2, void*), deref_address (f->esp, 3, unsigned));
        break;

  		case SYS_WRITE:
        printf("12\n");
  			check_user_program_addresses (deref_address (f->esp, 2, void*));
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
  thread_current ()->exit_status = status;
  printf ("%s: exit(%d)\n", thread_current ()->name,status);  //need name of process
  thread_exit ();
	NOT_REACHED ();
}

pid_t
exec (const char *file)
{
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
      thread_current ()->child_waiting_for = child_thread;
      sema_down (&thread_current ()->wait_sema);
      return child_thread->exit_status;
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
  lock_acquire(&filesys_lock);
  bool result = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return result;
}

bool
remove (const char *file)
{
  lock_acquire(&filesys_lock);
  bool result = filesys_remove(file);
  lock_release(&filesys_lock);
  return result;
}

int
open (const char *file)
{
  lock_acquire(&filesys_lock);
  int result = add_file_descriptor (filesys_open(file));
  printf("new fd assigned: %d\n", result);
  lock_release(&filesys_lock);
  return result;
}

int
filesize (int fd) 
{
  off_t length = -1;
  lock_acquire(&filesys_lock);

  struct file *file = get_file_struct(fd);

  if (file != NULL)
  {
    length = file_length (get_file_struct (fd));
  }

  lock_release(&filesys_lock);
  return length;
}

int
read (int fd, void *buffer, unsigned size)
{
  int result = -1;
  lock_acquire(&filesys_lock);

  struct file *file = get_file_struct(fd);

  /* error case: no file in fd */
  if (file != NULL)
  {
    result = file_read (get_file_struct (fd), buffer, size);
  }

  lock_release(&filesys_lock);
  return result;
}

/* Writes to open file or console. Returns the size of what was written. 
   Argument size may not equal the size written if space is limited. */
int
write (int fd, const void *buffer, unsigned size)
{

  lock_acquire(&filesys_lock);
  int size_written = 0;  //Nothing written

  struct file *file = get_file_struct(fd);
  
  /* Write to File (STDIN_FILENO) */
  // if(fd == 0)
  //   {
  //     size_written = file_write(file, buffer, size);  //Needs file name
  //   }
  /* Write to Console (STDOUT_FILENO) */
  if(fd == 1) 
    { 
      putbuf (buffer, size);
      size_written = size;  //Entire buffer written to console
    }
  else if (fd > 1)
    {
      size_written = file_write(file, buffer, size);  //Needs file name
    }

  lock_release(&filesys_lock);
  return size_written;
}

void
seek (int fd, unsigned position) 
{
  lock_acquire(&filesys_lock);
  struct file *file = get_file_struct(fd);
  file_seek (file, position);
  lock_release(&filesys_lock);
}

unsigned
tell (int fd) 
{
  struct file *file = get_file_struct(fd);
  return file_tell (file);
}

void
close (int fd)
{
  lock_acquire(&filesys_lock);
  struct file *file = get_file_struct(fd);
  file_close (file);
  remove_file_descriptor (fd);
  lock_release(&filesys_lock);
}

static void
check_user_program_addresses (void *address)
{
  // printf("1 %d\n", address);
  // printf("1 %p\n", address);

  // printf("2 %d\n", pagedir_get_page (thread_current ()->pagedir, address));
  // printf("2 %p\n", pagedir_get_page (thread_current ()->pagedir, address));

  //if (!is_user_vaddr(address) || pagedir_get_page (thread_current ()->pagedir, address) == NULL)
          //exit (-1);  

}
