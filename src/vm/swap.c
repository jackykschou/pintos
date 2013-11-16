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

/* Lock for the swap table. */
static struct lock swap_table_lock;

/* Function declaration. */
static uint32_t swap_table_get_free_page ();

/* Initialize the swap table. */
void
swap_table_init ()
{
	lock_init (&swap_table_lock);
	swap_block = block_get_role (BLOCK_SWAP);
	swap_table = bitmap_create (SWAP_BLOCK_PAGE_NUM);
}

/* Gets page from swap disk */
void
swap_table_swap_in (uint32_t idx, void *upage)
{
	int i;
	block_sector_t sector_idx;

	lock_acquire (&swap_table_lock);

	/* Read 8 blocks, which is a page. */
	sector_idx = idx * SECTORS_PER_PAGE;
	for (i = 0; i < SECTORS_PER_PAGE; ++i)
		{
			block_read (swap_block, sector_idx + i, upage);
			upage += BLOCK_SECTOR_SIZE;
		}

	/* Set the bits of the swap table to unused after swapping out. */
	bitmap_set (swap_table, idx, false);
	lock_release (&swap_table_lock);
}

/* Stores page UPAGE in swap disk. Returns the page index stored in the block device. */
uint32_t
swap_table_swap_out (const void *upage)
{
	int i;
	uint32_t block_page_idx;
	block_sector_t sector_idx;

	lock_acquire (&swap_table_lock);

	block_page_idx = swap_table_get_free_page ();
	sector_idx = block_page_idx * SECTORS_PER_PAGE;
	for (i = 0; i < SECTORS_PER_PAGE; ++i)
		{
			block_write (swap_block, sector_idx + i, upage);
			upage += BLOCK_SECTOR_SIZE;
		}

	/* Set the bits of the swap table to used after swapping out. */
	bitmap_set (swap_table, block_page_idx, true);
	lock_release (&swap_table_lock);

	return block_page_idx;
}

/* Free a page in the swap table. */
void
swap_table_free (uint32_t index)
{
	lock_acquire (&swap_table_lock);
	bitmap_set (swap_table, index, false);
	lock_release (&swap_table_lock);
}

/* Destroy the swap table. */
void
swap_table_destroy ()
{
	bitmap_destroy (swap_table); 
}

/* Obtain a free page space from the swap disk.
	Return the page index of the disk.
	Panic the kernel if the swap disk is full already.
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
		/* Panic kernel is there is no more space to swap to the disk. */
		PANIC ("Swap table is full!");
		return -1;
}

