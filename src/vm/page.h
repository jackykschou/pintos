#include <hash.h>

struct supp_page
  {
    struct hash_elem hash_elem; 		/* Hash table element. */
    uintptr_t upage;								/* Address of the page. */
    bool is_zero_bytes;							/* Indicates if the bytes of the page all zero at the point of loading. */
    bool is_residence;							/* Indicates if the page is in memory. (false if it is swapped out or not loaded, true if in main memory) */
    bool is_loaded;									/* Indicates if the page is loaded for the first time. */
  };

void supp_page_table_init (struct hash *table);
void supp_page_table_insert (struct hash *table, uintptr_t *upage, bool zeroed);
bool supp_page_table_inspect (struct hash *table, uintptr_t *vaddr);
void supp_page_table_destroy (struct hash *table);





