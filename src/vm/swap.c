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

static struct lock swap_table_lock;

static uint32_t swap_table_get_free_page ();

/* Initialize the swap table. */
void
swap_table_init ()
{
	lock_init (&swap_table_lock);
	swap_block = block_get_role (BLOCK_SWAP);
	swap_table = bitmap_create (SWAP_BLOCK_PAGE_NUM);
}

/* Get page from swap disk */
void
swap_table_swap_in (uint32_t idx, void *kpage)
{
	lock_acquire (&swap_table_lock);

	int i;
	block_sector_t sector_idx = idx * SECTORS_PER_PAGE;
	for (i = 0; i < SECTORS_PER_PAGE; ++i)
		{
			block_read (swap_block, sector_idx + i, kpage);
			kpage += BLOCK_SECTOR_SIZE;
		}

	bitmap_set (swap_table, idx, false);

	lock_release (&swap_table_lock);
}

/* Store page in swap disk, return the page index stored in the block device. */
uint32_t
swap_table_swap_out (const void *kpage)
{
	lock_acquire (&swap_table_lock);

	int i;
	uint32_t block_page_idx = swap_table_get_free_page ();
	block_sector_t sector_idx = block_page_idx * SECTORS_PER_PAGE;
	for (i = 0; i < SECTORS_PER_PAGE; ++i)
		{
			block_write (swap_block, sector_idx + i, kpage);
			kpage += BLOCK_SECTOR_SIZE;
		}

	bitmap_set (swap_table, block_page_idx, true);

	lock_release (&swap_table_lock);

	return block_page_idx;
}

/* Destroy the swap table. */
void
swap_table_destroy ()
{
	bitmap_destroy (swap_table); 
}

/* Obtain a free page space from the swap disk.
	Return the page index of the disk.
	Panic the machine if the swap disk is full already.
	*/
static uint32_t
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
		PANIC ("Swap table is full!");
		return -1;
}

