#include <debug.h>
#include "page.h"
#include "frame.h"
#include "swap.h"
#include "threads/vaddr.h"

#define MAX_USR_FRAME_NUM 383 /* Maximum number of frames in pintos. */

static bool install_page (struct thread *t, void *upage, void *kpage, bool writable);
static int do_eviction (struct thread *t, uint8_t *new_upage, bool writable);
static int get_victim ();

/* The frame table, which contains 383 (number of physical/kernel pages in pintos) entries. */
struct frame_table_entry frame_table[MAX_USR_FRAME_NUM];

/* Lock for accessing the frame table. */
static struct lock frame_table_lock;

/* Index of the victim during eviction. */
int next_victim = 0;

/* Initialization of the frame table.  */
void 
frame_table_init ()
{
	lock_init (&frame_table_lock);

	int i;
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
			frame_table[i].t = NULL;
			frame_table[i].upage = NULL;
			frame_table[i].kpage = palloc_get_page (PAL_USER | PAL_ZERO);
			frame_table[i].pin = false;
		}
}

/* Assigns a physical frame to a user page, if there are no more empty frame, do eviction and replacement. */
int
frame_table_assign_frame (struct thread *t, uint8_t *upage, bool writable, bool pin)
{
	ASSERT (upage != NULL);
	int i;

	lock_acquire (&frame_table_lock);
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
		if (frame_table[i].t == NULL)
			{
				frame_table[i].t = t;
				frame_table[i].upage = upage;
				ASSERT (install_page (t, upage, frame_table[i].kpage, writable));
				frame_table[i].pin = pin;
				lock_release(&frame_table_lock);
				return i;
			}
		}
	/* If the program reaches here, this means the frame table is full, performs eviction. */
	int index = do_eviction (t, upage, writable);
	lock_release(&frame_table_lock);
	return index;
}

/* Unpins a frame in the frame table. */
void
frame_table_unpin_frame (int index)
{
	lock_acquire (&frame_table_lock);
	frame_table[index].pin = false;
	lock_release (&frame_table_lock);
}

/* Clears all the physical frames of the current thread. */
void
frame_table_free_thread_frames ()
{
	struct thread *t = thread_current ();
	int i;

	lock_acquire(&frame_table_lock);
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
		if (frame_table[i].t == t)
			{
				memset (frame_table[i].kpage, 0, PGSIZE);
				pagedir_clear_page (t->pagedir, frame_table[i].upage);
				frame_table[i].t = NULL;
				frame_table[i].upage = NULL;
				frame_table[i].pin = true;
			}
		}
	lock_release (&frame_table_lock);
}


/* Deallocates the resources of the frame table. */
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

/* Performs an eviction given the NEW_UPAGE to be loaded. */
static int
do_eviction (struct thread *t, uint8_t *new_upage, bool writable)
{
	/* Get the victim index. */
	int victim_index = get_victim ();

	/* Find the corresponding supplemental page table entry. */
	struct supp_page* entry = supp_page_table_find_entry (&(frame_table[victim_index].t->supp_page_table), frame_table[victim_index].upage);

	ASSERT (entry != NULL);

	/* If the page is dirty, swap it out to the swap disk, otherwise just unload it. */
	if (pagedir_is_dirty (frame_table[victim_index].t->pagedir, frame_table[victim_index].upage) || entry->is_stack)
		{
			entry->block_page_idx = swap_table_swap_out (frame_table[victim_index].upage);
			entry->is_in_swap = true;
		}
	else
		{
			entry->is_loaded = false;
		}

	/* Clear out the victim page and load in the new page. */
	memset (frame_table[victim_index].kpage, 0, PGSIZE);
	pagedir_clear_page (frame_table[victim_index].t->pagedir, frame_table[victim_index].upage);
	frame_table[victim_index].t = t;
	frame_table[victim_index].upage = new_upage;
	frame_table[victim_index].pin = false;
	ASSERT (install_page (t, new_upage, frame_table[victim_index].kpage, writable));

	return victim_index;

}

/* Returns the index of the victim to evict from the frame table. (Clock replacement)*/
static int
get_victim ()
{
	int victim = (next_victim++) % MAX_USR_FRAME_NUM;
	/* Do not get victim that is pineed or accessed recently (set accessed bit to zero if accessed recently) */
	while (frame_table[victim].pin == true || pagedir_is_accessed (frame_table[victim].t->pagedir, frame_table[victim].upage) )
	{
		pagedir_set_accessed (frame_table[victim].t->pagedir, frame_table[victim].upage, false);
		victim = (next_victim++) % MAX_USR_FRAME_NUM;
	}
	return victim;
}