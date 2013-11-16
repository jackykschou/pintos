#include <debug.h>
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

static bool supp_page_table_load_page (struct hash *table, struct supp_page *entry);
static void supp_page_table_destructor (struct hash_elem *e, void *aux);
static unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
static void swap_in_page_from_disk (struct supp_page* entry);


/* Initialization of a supplemental page table. */
void supp_page_table_init (struct hash *table)
{
	ASSERT (hash_init (table, page_hash, page_less, NULL) );
}

/* Add a new supplemental page table entry. */
void
supp_page_table_insert (struct hash *table, uintptr_t upage, size_t page_read_bytes, bool writable, off_t offset, bool is_stack)
{
	ASSERT (upage % PGSIZE == 0);

	struct supp_page *supp_page_entry = (struct supp_page*) malloc (sizeof (struct supp_page));

	supp_page_entry->upage = upage;
	supp_page_entry->page_read_bytes = page_read_bytes;
	supp_page_entry->writable = writable;
	if (is_stack)
		supp_page_entry->is_loaded = true;
	else
		supp_page_entry->is_loaded = false;
	supp_page_entry->is_in_swap = false;
	supp_page_entry->is_stack = is_stack;
	supp_page_entry->offset = offset;
	hash_insert (table, &supp_page_entry->hash_elem);
}

/* Inspects supplemental page table TABLE given the virtual address VADDR. 
		If the page in that entry is not loaded yet, load it. If the page is
		in the swap disk, swap it back to main memory.
		If the page is successfully loaded (from either swap disk or filesys), 
		return true, else return false. */
bool
supp_page_table_inspect (struct hash *table, uintptr_t vaddr)
{
	struct supp_page *entry = supp_page_table_find_entry (table, pg_round_down (vaddr));

	if (entry == NULL)
		{
			return false;
		}
	else
		{
			/* If the page is not loaded yet and not swapped into the disk, load it */
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

			/* If the page is in memory and is being inspected, this means that the process tries to write to non-writeable memory,
				terminate the process. */
			else if (entry->is_loaded && !entry->is_in_swap)
				{
					exit (-1);
				}

			NOT_REACHED ();
			return false;
		}
}

/* Returns the supplemental page entry given the page table TABLE and the user virtual address VADDR to get. */
struct supp_page *
supp_page_table_find_entry (struct hash *table, uintptr_t vaddr)
{
	struct supp_page *supp_page_entry_copy;
	struct hash_elem *elem;

	supp_page_entry_copy = (struct supp_page *)malloc (sizeof (struct supp_page));
	supp_page_entry_copy->upage = vaddr;
	elem =  hash_find (table, &supp_page_entry_copy->hash_elem);
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
	struct thread *thread_cur;
	uint8_t *upage;
	uint8_t *kpage;
	int index;

	thread_cur = thread_current ();
  upage = entry->upage;
  
  /* Get a page of memory and pin it for loading in the executable. */
  index = frame_table_assign_frame (thread_cur, entry->upage, entry->writable, true);
	kpage = pagedir_get_page (thread_cur->pagedir, entry->upage);
  file_seek (thread_cur->executable, entry->offset);

  /* Load the executable to the page. */
  if (file_read (thread_cur->executable, kpage, entry->page_read_bytes) != (int) entry->page_read_bytes)
    {
    	exit (-1);
      return false;
    }

  /* Set the remaining unload bytes of the page to 0. */
  memset (kpage + entry->page_read_bytes, 0,  PGSIZE - entry->page_read_bytes);

  /* Unpin the frame after finish loading. */
  frame_table_unpin_frame (index);

  /* Set the supplmental page entry to loaded. */
  entry->is_loaded = true;
	return true;

}

/* Destructs the hash table of the supplemental page table. */
static void
supp_page_table_destructor (struct hash_elem *e, void *aux)
{
	struct supp_page *entry;

	entry = hash_entry (e, struct supp_page, hash_elem);
	if (entry->is_in_swap)
		{
			swap_table_free (entry->block_page_idx);
		}
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
	const struct supp_page *a;
  const struct supp_page *b;

  a = hash_entry (a_, struct supp_page, hash_elem);
  b = hash_entry (b_, struct supp_page, hash_elem);
  return a->upage < b->upage;
}

/* Swaps a page back from the swap disk to the main memory. */
static void
swap_in_page_from_disk (struct supp_page* entry)
{
	int index;
	/* Get a frame for the page in the swap disk and pin it for getting data from the swap table to the main memory. */
	index = frame_table_assign_frame (thread_current(), entry->upage, entry->writable, true);
	entry->is_in_swap = false;
	swap_table_swap_in (entry->block_page_idx, entry->upage);
	/* Unpin the page after data has transferred. */
	frame_table_unpin_frame (index);
}
