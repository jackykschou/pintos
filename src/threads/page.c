#include <debug.h>
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "frame.h"
#include "page.h"

void
supp_page_table_init (struct hash *table)
{
		ASSERT (hash_init (table, page_hash, page_less, NULL) );
}

void
supp_page_table_insert (struct hash *table, uintptr_t *upage, bool zeroed)
{
	struct supp_page *supp_page_entry = (supp_page*) malloc (sizeof (supp_page));

	supp_page_entry->upage = upage;
	supp_page_entry->is_zero_bytes = zeroed;
	supp_page_entry->is_loaded = false;
	supp_page_entry->is_residence = false;

	hash_insert (table, supp_page_entry->hash_elem);
}

bool
supp_page_table_inspect (struct hash *table, uintptr_t *vaddr)
{
	struct supp_page *supp_page_entry_copy = (supp_page*) malloc (sizeof (supp_page));

	supp_page_entry_copy->upage = pg_round_down (vaddr);

	struct hash_elem *elem =  hash_find (table, &supp_page_entry_copy->hash_elem);

	free (supp_page_entry_copy);

	/*If the page does not exists, return false. */
	if (elem == NULL)
		{
			retrun false;
		}
	else
		{
			// struct supp_page *entry = hash_entry (a_, struct supp_page, entry);

			// /* If the page is not loaded yet, load it */
			// if (!entry->is_loaded)
			// 	{

			// 		/* Get a page of memory. */
   //    uint8_t *kpage = palloc_get_page (PAL_USER);
   //    assign_frame (thread_current (), kpage, upage);
   //    if (kpage == NULL)
   //      return false;

   //    /* Load this page. */
   //    if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
   //      {
   //        free_frame (thread_current (), kpage);
   //        palloc_free_page (kpage);
   //        return false; 
   //      }
   //    memset (kpage + page_read_bytes, 0, page_zero_bytes);

   //    /* Add the page to the process's address space. */
   //    if (!install_page (upage, kpage, writable)) 
   //      {
   //        free_frame (thread_current (), kpage);
   //        palloc_free_page (kpage);
   //        return false; 
   //      }
			return true;
				}
			/* If the page is swapped out, swapped it back. */
			else
				{
					return true;
				}
		}
}

void
supp_page_table_destroy (struct hash *table) 
{
  hash_destroy (table, supp_page_table_destructor);
}


static void
supp_page_table_load_page (uintptr_t *upage)
{

}

static void
supp_page_table_destructor (struct hash_elem *e, void *aux)
{
	struct supp_page *entry = hash_entry (a_, struct supp_page, hash_elem);
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

