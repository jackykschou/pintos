#include <debug.h>
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"

static bool supp_page_table_load_page (struct hash *table, struct supp_page *entry);
static void supp_page_table_destructor (struct hash_elem *e, void *aux);
static unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
static void swap_in_page_from_disk (struct supp_page* entry);

/* Initialization of supplmental page table. */
void supp_page_table_init (struct hash *table)
{
		ASSERT (hash_init (table, page_hash, page_less, NULL) );
}

/* Add a new supplmental page table entry. */
void
supp_page_table_insert (struct hash *table, uintptr_t upage, size_t page_read_bytes, bool writable, off_t offset)
{
	struct supp_page *supp_page_entry = (struct supp_page*) malloc (sizeof (struct supp_page));

	supp_page_entry->upage = upage;
	supp_page_entry->page_read_bytes = page_read_bytes;
	supp_page_entry->writable = writable;
	supp_page_entry->is_loaded = false;
	supp_page_entry->is_in_swap = false;
	supp_page_entry->offset = offset;

	hash_insert (table, &supp_page_entry->hash_elem);

}

/* Inspect an supplmental page table entry given a process virtual address. 
		If the page in that entry is not loaded yet, load it. If the page is
		in the swap disk, swap it back to main memory.
		If the page is successfull loaded (from either swap disk or filesys), 
		return true, else return false. */
bool
supp_page_table_inspect (struct hash *table, uintptr_t vaddr)
{
	struct supp_page *entry = supp_page_table_find_entry (table, vaddr);
	if (entry == NULL)
		{
			return false;
		}
	else
		{
			/* If the page is not loaded yet and not, load it */
			if (!entry->is_loaded && !entry->is_in_swap)
				{
					return supp_page_table_load_page (table, entry);
				}
			/* If the page is swapped out, swapped it back. */
			else if (entry->is_loaded && entry->is_in_swap)
				{
					swap_in_page_from_disk (entry);
					return true;
				}

			NOT_REACHED ();
			return false;
		}
}

struct supp_page*
supp_page_table_find_entry (struct hash *table, uintptr_t vaddr)
{
	struct supp_page *supp_page_entry_copy = (struct supp_page *)malloc (sizeof (struct supp_page));
	supp_page_entry_copy->upage = pg_round_down (vaddr);

	struct hash_elem *elem =  hash_find (table, &supp_page_entry_copy->hash_elem);

	free (supp_page_entry_copy);

	if (elem == NULL)
		return NULL;
	else
		return hash_entry (elem, struct supp_page, hash_elem);
}

/* Free all the frames occupied by any process virtual address in the frame table
	and dellocate resources for the hash table of the supplmental page table. */
void
supp_page_table_destroy (struct hash *table) 
{
	frame_table_free_thread_frames ();
  hash_destroy (table, supp_page_table_destructor);
}

/* Load a page of the executable file of the process from the disk.
	Return true if success, false otherwise. */
static bool
supp_page_table_load_page (struct hash *table, struct supp_page *entry)
{
	struct thread *thread_cur = thread_current ();

	/* Get a page of memory. */
  uint8_t *upage = entry->upage;
  frame_table_assign_frame (entry->upage, entry->writable);

	uint8_t *kpage = pagedir_get_page (thread_cur->pagedir, entry->upage);
  file_seek (thread_cur->executable, entry->offset);

  /* Load the executable to the page. */
  if (file_read (thread_cur->executable, kpage, entry->page_read_bytes) != (int) entry->page_read_bytes)
    {
    	frame_table_free_frame (kpage);
    	exit (-1);
      return false;
    }

  memset (kpage + entry->page_read_bytes, 0,  PGSIZE - entry->page_read_bytes);

  entry->is_loaded = true;
	return true;

}

/* Destructor for the hash table of the supplmental page table. */
static void
supp_page_table_destructor (struct hash_elem *e, void *aux)
{
	struct supp_page *entry = hash_entry (e, struct supp_page, hash_elem);
	free (entry);
}

/* Returns a hash value for page p. */
static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct supp_page *p = hash_entry (p_, struct supp_page, hash_elem);
  return hash_bytes (&p->upage, sizeof (p->upage));
}

/* Returns true if page a precedes page b. */
static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct supp_page *a = hash_entry (a_, struct supp_page, hash_elem);
  const struct supp_page *b = hash_entry (b_, struct supp_page, hash_elem);

  return a->upage < b->upage;
}

static void
swap_in_page_from_disk (struct supp_page* entry)
{
	swap_table_swap_in (entry->block_page_idx, pagedir_get_page (thread_current()->pagedir, entry->upage));
	frame_table_assign_frame (entry->upage, entry->writable);
	entry->is_in_swap = false;
}
