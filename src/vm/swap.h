#include <bitmap.h>

typedef int block_page_index;

void swap_table_init ();
void swap_table_swap_in (block_page_index idx, void *kpage);
block_page_index swap_table_swap_out (const void *kpage);
void swap_table_destroy ();
