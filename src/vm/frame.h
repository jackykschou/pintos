#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/thread.h"

struct frame_table_entry
{
	struct thread *t;		/* Process that is using the frame. */
	uint8_t *upage;		/* Virtual page address that is possibly mapped to the frame. */
	uint8_t *kpage;		/* Physical address of the frame. */
};

void frame_table_init (void);
void frame_table_assign_frame (struct thread *t, uint8_t *upage, bool writable);
void frame_table_free_frame (struct thread *t, uint32_t *kpage);
void frame_table_free_thread_frames (void);
void frame_table_destroy (void);
