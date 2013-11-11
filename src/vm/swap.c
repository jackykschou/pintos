#include <debug.h>
#include <hash.h>
#include <bitmap.h>
#include "devices/block.h"
#include "frame.h"
#include "page.h"
#include "swap.h"

#define SWAP_BLOCK_PAGE_NUM 1024
#define SECTORS_PER_PAGE 8

/* Swap block device. */
struct block *swap_block;

/* Swap table (keep track of which 8 sectors (interpreted as page) are free 
or not in the swap block device). */
struct bitmap *swap_table;

static block_page_index swap_table_get_free_page ();

/* Initialize the swap table. */
void
swap_table_init ()
{
	swap_block = block_get_role (BLOCK_SWAP);
	swap_table = bitmap_create (SWAP_BLOCK_PAGE_NUM);
}

/* Get page from swap disk */
void
swap_table_swap_in (block_page_index idx, void *kpage)
{
	int i;
	block_sector_t sector_idx = idx * SECTORS_PER_PAGE;
	for (i = 0; i < SECTORS_PER_PAGE; ++i)
		{
			block_read (swap_block, sector_idx + i, kpage);
			kpage += BLOCK_SECTOR_SIZE;
		}

	bitmap_set (swap_table, idx, false);
}

/* Store page in swap disk, return the page index stored in the block device. */
block_page_index
swap_table_swap_out (const void *kpage)
{
	int i;
	block_page_index block_page_idx = swap_table_get_free_page ();
	block_sector_t sector_idx = block_page_idx * SECTORS_PER_PAGE;
	for (i = 0; i < SECTORS_PER_PAGE; ++i)
		{
			block_write (swap_block, sector_idx + i, kpage);
			kpage += BLOCK_SECTOR_SIZE;
		}

	bitmap_set (swap_table, block_page_idx, true);

	return block_page_idx;
}

/* Destroy the swap table. */
void
swap_table_destroy ()
{
	bitmap_destroy (swap_table); 
}

static block_page_index
swap_table_get_free_page ()
{
	int i;
	for (i = 0; i < SWAP_BLOCK_PAGE_NUM; ++i)
		{
			if (!bitmap_test (swap_table, i))
				{
					return i;
				}
		}
		PANIC (Swap table is full!);
		return -1;
}

