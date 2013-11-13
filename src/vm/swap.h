#include <bitmap.h>

extern struct bitmap *swap_table;

void swap_table_init ();
void swap_table_swap_in (uint32_t idx, void *kpage);
uint32_t swap_table_swap_out (const void *kpage);
void swap_table_destroy ();
