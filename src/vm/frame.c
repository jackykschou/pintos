#include <debug.h>
#include "frame.h"

#define MAX_USR_FRAME_NUM 383 /* Numbwe of frames in pintos. */

struct frame_table_entry frame_table[MAX_USR_FRAME_NUM];

static struct lock frame_table_lock;

static bool install_page (void *upage, void *kpage, bool writable);

/* Initialize the frame table  */
void 
frame_table_init ()
{
	lock_init (&frame_table_lock);

	uint32_t *frame;
	bool ok = true;
	int num = 0;
	int i;
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
			frame_table[i].t = NULL;
			frame_table[i].upage = NULL;
			frame_table[i].kpage = palloc_get_page (PAL_USER);
		}
}

/* Try to assign a physical frame to a user page, if fails, return NULL. */
uint8_t *
frame_table_assign_frame (struct thread *t, struct supp_page *entry)
{
	lock_acquire (&frame_table_lock);

	ASSERT (entry->upage != NULL);

	bool empty_frame_found = false;
	int i;
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
		if (frame_table[i].t == NULL)
			{
				empty_frame_found = true;
				frame_table[i].t = t;
				frame_table[i].upage = entry->upage;
				ASSERT (install_page (entry->upage, frame_table[i].kpage, entry->writable));
				break;
			}
		}

	lock_release(&frame_table_lock);
	
	if (!empty_frame_found)
		{
			return NULL;
		}
}

void
frame_table_free_frame (struct thread *t, uint32_t *kpage)
{
	lock_acquire(&frame_table_lock);

	ASSERT (t != NULL);

	bool frame_found = false;
	int i;
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
		if (frame_table[i].kpage == kpage && frame_table[i].t == t)
			{
				frame_found = true;
				frame_table[i].t = NULL;
				frame_table[i].upage = NULL;
				pagedir_clear_page (t->pagedir, frame_table[i].upage);
				break;
			}
		}

	ASSERT (frame_found);

	lock_release(&frame_table_lock);
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


