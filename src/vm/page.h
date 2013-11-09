#include <hash.h>

struct supp_page
  {
    struct hash_elem hash_elem; 		/* Hash table element. */
    uintptr_t upage;								/* Address of the page. */
    bool is_residence;							/* Indicates if the page is in memory. (false if it is swapped out or not loaded, true if in main memory) */
    bool is_loaded;									/* Indicates if the page is loaded for the first time. */
    bool writable;									/* Indicates if the page is writable. */
    size_t page_read_bytes;					/* Number of bytes to read when the page is loaded. */
  };

void supp_page_table_init (struct hash *table);
void supp_page_table_insert (struct hash *table, uintptr_t upage, size_t page_read_bytes, bool writable);
//bool supp_page_table_inspect (struct hash *table, uintptr_t vaddr, struct intr_frame *f);
void supp_page_table_destroy (struct hash *table);





