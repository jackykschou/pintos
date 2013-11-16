#include <bitmap.h>

/* Function declaractions. */
void swap_table_init ();
void swap_table_swap_in (uint32_t idx, void *upage);
uint32_t swap_table_swap_out (const void *upage);
void swap_table_free (uint32_t index);
void swap_table_destroy ();

