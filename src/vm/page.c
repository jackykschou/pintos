#include <debug.h>
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "frame.h"
#include "page.h"

static bool supp_page_table_load_page (struct hash *table, struct supp_page *entry);
static void supp_page_table_destructor (struct hash_elem *e, void *aux);
static unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
static bool install_page (void *upage, void *kpage, bool writable);


void supp_page_table_init (struct hash *table)
{
		ASSERT (hash_init (table, page_hash, page_less, NULL) );
}

void
supp_page_table_insert (struct hash *table, uintptr_t upage, size_t page_read_bytes, bool writable, off_t offset)
{
	struct supp_page *supp_page_entry = (struct supp_page*) malloc (sizeof (struct supp_page));

	supp_page_entry->upage = upage;
	supp_page_entry->page_read_bytes = page_read_bytes;
	supp_page_entry->writable = writable;
	supp_page_entry->is_loaded = false;
	supp_page_entry->is_residence = false;
	supp_page_entry->offset = offset;

	hash_insert (table, &supp_page_entry->hash_elem);

}

bool
supp_page_table_inspect (struct hash *table, uintptr_t vaddr, struct intr_frame *f)
{
	struct supp_page *supp_page_entry_copy = (struct supp_page *)malloc (sizeof (struct supp_page));
	supp_page_entry_copy->upage = pg_round_down (vaddr);

	struct hash_elem *elem =  hash_find (table, &supp_page_entry_copy->hash_elem);

	free (supp_page_entry_copy);

	/*If the page does not exist, return false. */
	if (elem == NULL)
		{
			return false;
		}
	else
		{
			struct supp_page *entry = hash_entry (elem, struct supp_page, hash_elem);

			/* If the page is not loaded yet, load it */
			if (!entry->is_loaded)
				{
					return supp_page_table_load_page (table, entry);
				}
			/* If the page is swapped out, swapped it back. */
			else
				{
					return false;
				}
		}
}

void
supp_page_table_destroy (struct hash *table) 
{
  hash_destroy (table, supp_page_table_destructor);
}

static bool
supp_page_table_load_page (struct hash *table, struct supp_page *entry)
{
	struct thread *thread_cur = thread_current ();

	/* Get a page of memory. */
  uint8_t *kpage = palloc_get_page (PAL_USER);
  uint8_t *upage = entry->upage;

  if (kpage == NULL)
  {
  	printf("pt1\n");
  	return false;
  }

  assign_frame (thread_cur, kpage, upage);

  file_seek (thread_cur->executable, entry->offset);
  /* Load the executable to the page. */
  if (file_read (thread_cur->executable, kpage, entry->page_read_bytes) != (int) entry->page_read_bytes)
    {
    	printf("pt2: %d\n", entry->page_read_bytes);
      free_frame (thread_cur, kpage);
      palloc_free_page (kpage);
      return false; 
    }

  memset (kpage + entry->page_read_bytes, 0,  PGSIZE - entry->page_read_bytes);

  if (!install_page (entry->upage, kpage, entry->writable)) 
    {
    	printf("pt3\n");
      free_frame (thread_cur, kpage);
      palloc_free_page (kpage);
      return false; 
    }

  entry->is_loaded = true;
	return true;
}

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

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
