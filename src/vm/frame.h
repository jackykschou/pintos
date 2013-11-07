#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/thread.h"

struct frame_table_entry
{
	struct thread *t;		/* Process that is using the frame. */
	uint32_t *upage;		/* Virtual page address that is possibly mapped to the frame. */
	uint32_t *kpage;		/* Physical address of the frame. */
};

void frame_table_init (void);
void assign_frame (struct thread *t, uint32_t *kpage, uint32_t *upage);
void free_frame (struct thread *t, uint32_t *kpage);
