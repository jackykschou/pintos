#include <debug.h>
#include "page.h"
#include "frame.h"
#include "swap.h"
#include "threads/vaddr.h"

#define MAX_USR_FRAME_NUM 383 /* Number of frames in pintos. */

struct frame_table_entry frame_table[MAX_USR_FRAME_NUM];

static struct lock frame_table_lock;
static struct lock eviction_lock;

static bool install_page (struct thread *t, void *upage, void *kpage, bool writable);
static void do_eviction (struct thread *t, uint8_t *new_upage, bool writable);
static int get_victim ();

int next_victim = 0;

/* Initialize the frame table  */
void 
frame_table_init ()
{
	lock_init (&frame_table_lock);
	lock_init (&eviction_lock);

	int i;
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
			frame_table[i].t = NULL;
			frame_table[i].upage = NULL;
			frame_table[i].kpage = palloc_get_page (PAL_USER | PAL_ZERO);
		}
}

/* Assign a physical frame to a user page, if there are no more empty frame, do eviction and replacement. */
void
frame_table_assign_frame (struct thread *t, uint8_t *upage, bool writable)
{
	ASSERT (upage != NULL);
	bool frame_found = false;
	int i;
	lock_acquire (&frame_table_lock);
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
		if (frame_table[i].t == NULL)
			{
				frame_table[i].t = t;
				// printf("upage in frame: %p\n", upage);
				frame_table[i].upage = upage;
				ASSERT (install_page (t, upage, frame_table[i].kpage, writable));
				lock_release(&frame_table_lock);
				return;
			}
		}
	lock_release(&frame_table_lock);
	do_eviction (t, upage, writable);
}

/* Clear a physical frame given a virtual address of the current thread for usage later on. */
void
frame_table_free_frame (struct thread *t, uint32_t *kpage)
{
	bool frame_found = false;
	int i;
	lock_acquire(&frame_table_lock);
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
		if (frame_table[i].kpage == kpage && frame_table[i].t == t)
			{
				memset (frame_table[i].kpage, 0, PGSIZE);
				pagedir_clear_page (t->pagedir, frame_table[i].upage);
				frame_found = true;
				frame_table[i].t = NULL;
				frame_table[i].upage = NULL;
				break;
			}
		}
	lock_release(&frame_table_lock);
	ASSERT (frame_found);
}

/* Clear all the physical frames of the current thread. */
void
frame_table_free_thread_frames ()
{
	struct thread *t = thread_current ();
	int i;
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
		if (frame_table[i].t == t)
			{
				memset (frame_table[i].kpage, 0, PGSIZE);
				pagedir_clear_page (t->pagedir, frame_table[i].upage);
				frame_table[i].t = NULL;
				frame_table[i].upage = NULL;
			}
		}
}


/* Dellocate resources the frame table. */
void
frame_table_destroy ()
{
	int i;
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
			palloc_free_page (frame_table[i].kpage);
		}
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (struct thread *t, void *upage, void *kpage, bool writable)
{
  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

static void
do_eviction (struct thread *t, uint8_t *new_upage, bool writable)
{
	// lock_acquire (&eviction_lock);
	int victim_index = get_victim ();
	struct supp_page* entry = supp_page_table_find_entry (&(frame_table[victim_index].t->supp_page_table), frame_table[victim_index].upage);
	ASSERT (entry != NULL);
	// printf("swap away address: %p\n", frame_table[victim_index].upage);
	if (pagedir_is_dirty (frame_table[victim_index].t->pagedir, frame_table[victim_index].upage) || entry->is_stack)
		{
			// printf("swap out address: %p\n", frame_table[victim_index].upage);
			entry->block_page_idx = swap_table_swap_out (frame_table[victim_index].upage);
			entry->is_in_swap = true;
		}
	else
	{
		entry->is_loaded = false;
	}
	frame_table_free_frame (frame_table[victim_index].t, pagedir_get_page (frame_table[victim_index].t->pagedir, frame_table[victim_index].upage));
	frame_table_assign_frame (thread_current (), new_upage, writable);
	// lock_release (&eviction_lock);
}

static int
get_victim ()
{
	return (next_victim++) % MAX_USR_FRAME_NUM;

	// int i;
	// for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
	// 	{
	// 	if (!pagedir_is_dirty (frame_table[i].t->pagedir, frame_table[i].upage))
	// 		{
	// 			return i;
	// 		}
	// 	}
	// return 0;
}