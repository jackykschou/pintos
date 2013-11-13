#include <hash.h>
#include "filesys/off_t.h"

struct supp_page
  {
    struct hash_elem hash_elem; 		/* Hash table element. */
    uintptr_t upage;					/* Address of the page. */
    bool is_in_swap;					/* Indicates if the page is in the swap disk */
    bool is_loaded;						/* Indicates if the page has been loaded from the filesys. */
    bool writable;						/* Indicates if the page is writable. */
    bool is_stack;                      /* Indicates if the page is allocated for the stack. */
    size_t page_read_bytes;				/* Number of bytes to read when the page is loaded. */
    off_t offset;						/* Offset of the executable should be read when load. */
    uint32_t block_page_idx;			/* The possible page index in the block device if the page is swapped in. */
  };

void supp_page_table_init (struct hash *table);
bool supp_page_table_inspect (struct hash *table, uintptr_t vaddr);
void supp_page_table_insert (struct hash *table, uintptr_t upage, size_t page_read_bytes, bool writable, off_t offset, bool is_stack);
void supp_page_table_destroy (struct hash *table);
struct supp_page* supp_page_table_find_entry (struct hash *table, uintptr_t vaddr);





