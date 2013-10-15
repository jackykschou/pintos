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

#define deref_address(ADDRESS, TYPE)                    \
        *((TYPE*) (ADDRESS))

static void syscall_handler (struct intr_frame *);
static void check_user_program_addresses (int num_args, void* address);

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

	switch (*(uint32_t*)(f->esp))
  	{
  		/* No arguments */
  		case SYS_HALT:
  			halt ();

  		/* One argument */
  		case SYS_EXIT:
  			check_user_program_addresses (1, f->esp);
  			exit (deref_address (f->esp + sizeof (uint32_t), int));

  		case SYS_EXEC:
  			check_user_program_addresses (1, f->esp);
  			//exec (deref_address (f->esp + sizeof (uint32_t), char*));

  		case SYS_WAIT:
  			check_user_program_addresses (1, f->esp);
  			//wait (f->esp + sizeof (uint32_t));

  		case SYS_REMOVE:
  			check_user_program_addresses (1, f->esp);
  			remove (f->esp + sizeof (uint32_t));

  		case SYS_OPEN:
  			check_user_program_addresses (1, f->esp);
  			open (f->esp + sizeof (uint32_t));

  		case SYS_FILESIZE:
  			check_user_program_addresses (1, f->esp);
  			//filesize (f->esp + sizeof (uint32_t));

  		case SYS_TELL:
  			check_user_program_addresses (1, f->esp);
  			//tell (f->esp + sizeof (uint32_t));

  		case SYS_CLOSE:
  			check_user_program_addresses (1, f->esp);
  			//close (f->esp + sizeof (uint32_t));

  	    /* Two arguments */
  		case SYS_CREATE:
  			check_user_program_addresses (2, f->esp);
  			//create (f->esp + sizeof (uint32_t), f->esp + sizeof (uint32_t) * 2);

  		case SYS_SEEK:
  			check_user_program_addresses (2, f->esp);
  			seek (f->esp + sizeof (uint32_t), f->esp + sizeof (uint32_t) * 2);

        /* Three arguments */
  		case SYS_READ:
  			check_user_program_addresses (3, f->esp);
  			//read (f->esp + sizeof (uint32_t), f->esp + sizeof (uint32_t) * 2, f->esp + sizeof (uint32_t) * 3);

  		case SYS_WRITE:
  			check_user_program_addresses (3, f->esp);
  			f->eax = write (deref_address (f->esp + sizeof (uint32_t), int), 
          deref_address (f->esp + sizeof (uint32_t) * 2, void*), deref_address (f->esp + sizeof (uint32_t) * 3, unsigned));

      default:
      	break;
  	}

    return;

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

// pid_t
// exec (const char *file)
// {
//   return NULL;
// }

// int
// wait (pid_t pid)
// {

// }

bool
create (const char *file, unsigned initial_size)
{
  lock_acquire(&filesys_lock);
  bool result = filesys_create(*file, initial_size);
  lock_release(&filesys_lock);
  return result;
}

bool
remove (const char *file)
{
  lock_acquire(&filesys_lock);
  bool result = filesys_remove(*file);
  lock_release(&filesys_lock);
  return result;
}

int
open (const char *file)
{
  lock_acquire(&filesys_lock);
  int result = filesys_open(*file);
  lock_release(&filesys_lock);
  return result;
}

// int
// filesize (int fd) 
// {
//   return NULL;
// }

// int
// read (int fd, void *buffer, unsigned size)
// {
//   int result = -1;
//   lock_acquire(&filesys_lock);
//   result = file_read(*file, *buffer, size);
//   lock_release(&filesys_lock);
//   return result;
// }

/* Writes to open file or console. Returns the size of what was written. 
   Argument size may not equal the size written if space is limited. */
int
write (int fd, const void *buffer, unsigned size)
{

  lock_acquire(&filesys_lock);
  int size_written = 0;  //Nothing written

    // // /* Write to File (STDIN_FILENO) */
    // // if(fd == 0)
    // // {
    // //   size_written = file_write( ,*buffer, size);  //Needs file name
    // // }
    /* Write to Console (STDOUT_FILENO) */
    if(fd == 1) 
      { 
        putbuf(buffer, size);
        size_written = size;  //Entire buffer written to console
      }

  lock_release(&filesys_lock);
  return size_written;
}

void
seek (int fd, unsigned position) 
{
  return NULL;
}

// unsigned
// tell (int fd) 
// {
//   return NULL;
// }

// void
// close (int fd)
// {
//   return NULL;
// }

// mapid_t
// mmap (int fd, void *addr)
// {
//   return NULL;
// }

// void
// munmap (mapid_t mapid)
// {
//   return NULL;
// }

// bool
// chdir (const char *dir)
// {
//   return NULL;
// }

// bool
// mkdir (const char *dir)
// {
//   return NULL;
// }

// bool
// readdir (int fd, char name[READDIR_MAX_LEN + 1]) 
// {
//   return NULL;
// }

// bool
// isdir (int fd) 
// {
//   return NULL;
// }

// int
// inumber (int fd) 
// {
//   return NULL;
// }


static void
check_user_program_addresses (int num_args, void* address)
{
	int i = 1;
	for (; i <= num_args; ++i)
		{
			if ((address + i * sizeof (uint32_t)) == NULL || !is_user_vaddr(address + i * sizeof (uint32_t)) 
				|| pagedir_get_page (thread_current ()->pagedir, address + i * sizeof (uint32_t)) == NULL){

          printf("failed user addr check");
          exit (-1);    

      }
		}
}