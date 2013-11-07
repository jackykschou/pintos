#include <debug.h>
#include "frame.h"

#define MAX_USR_FRAME_NUM 383 /* Numbwe of frames in pintos. */

struct frame_table_entry frame_table[MAX_USR_FRAME_NUM];

static struct lock frame_table_lock;

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
		
		for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
			palloc_free_page (frame_table[i].kpage);
		}
}

void
assign_frame (struct thread *t, uint32_t *kpage, uint32_t *upage)
{
	lock_acquire (&frame_table_lock);

	ASSERT (kpage != NULL);
	ASSERT (upage != NULL);

	bool frame_found = false;
	int i;
	for (i = 0; i < MAX_USR_FRAME_NUM; ++i)
		{
		if (frame_table[i].kpage == kpage && frame_table[i].t == NULL)
			{
				frame_found = true;
				frame_table[i].t = t;
				frame_table[i].upage = upage;
				break;
			}
		}

	ASSERT (frame_found);

	lock_release(&frame_table_lock);
}

void
free_frame (struct thread *t, uint32_t *kpage)
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
				break;
			}
		}

	ASSERT (frame_found);

	lock_release(&frame_table_lock);
}


