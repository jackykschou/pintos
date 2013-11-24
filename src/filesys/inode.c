#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define NUM_DIRECT_BLOCKS 124
#define NUM_DIRECT_BYTES (NUM_DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)
#define INDIRECT_BLOCK_SECTORS (BLOCK_SECTOR_SIZE / sizeof (block_sector_t))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t direct[NUM_DIRECT_BLOCKS];     /* Indicies for direct data blocks */
    block_sector_t indirect;                      /* Index to first-level index block */
    block_sector_t double_indirect;               /* Index to second-level index block */
    off_t length;                                 /* File size in bytes. */
    unsigned magic;                               /* Magic number. */
  };

struct indirect_block
  {
    block_sector_t direct[INDIRECT_BLOCK_SECTORS];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  /* Check invalid */
  if (pos > inode->data.length || pos < 0)
    return -1;

  block_sector_t sector_offset = pos / BLOCK_SECTOR_SIZE;

  /* If pos in within direct block */
  if (pos < NUM_DIRECT_BYTES)
    {
      return inode->data.direct[pos / BLOCK_SECTOR_SIZE];
    }

  /* Pos is within indirect block */
  else if (pos < NUM_DIRECT_BYTES + (INDIRECT_BLOCK_SECTORS * BLOCK_SECTOR_SIZE))
    { 
      /* Block buffers for indirect block. */
      struct indirect_block *block_buffer;

      block_read (fs_device, inode->data.indirect, block_buffer);

      return block_buffer->direct[sector_offset - NUM_DIRECT_BLOCKS - INDIRECT_BLOCK_SECTORS];
    }

  /* Pos is within double indirect block */
  else
    {
      /* Block buffers for first and second indirection levels for reading the second level indirect block*/
      struct indirect_block *block_buffer_l1;
      struct indirect_block *block_buffer_l2;

      block_sector_t double_indirect_block_index = (sector_offset - NUM_DIRECT_BLOCKS - INDIRECT_BLOCK_SECTORS) % INDIRECT_BLOCK_SECTORS;
      /* Obtain the indirect block which contains the sector index of the offset */
      block_read (fs_device, inode->data.double_indirect, block_buffer_l1);
      block_read (fs_device, block_buffer_l1->direct[double_indirect_block_index], block_buffer_l2);

      return block_buffer_l2->direct[sector_offset - NUM_DIRECT_BLOCKS - INDIRECT_BLOCK_SECTORS - 
             INDIRECT_BLOCK_SECTORS * double_indirect_block_index];
    }

}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  size_t sectors = bytes_to_sectors (length);
  disk_inode = calloc (1, sizeof *disk_inode);

  block_sector_t *indices = malloc (sectors * sizeof (block_sector_t));
  if (disk_inode != NULL && indices != NULL)
    {
      bool alloc_success = true;
      bool assign_index_success = true;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      /* Allocate sectors for the file data. */
      int i, sector_index;
      for (i = 0; i < sectors; ++i)
        {
          if (!free_map_allocate (1, &sector_index))
            {
              alloc_success = false
              break;
            }
            indices[i] = sector_index;
        }
      /* If allocation fails, deallocate all the sectors that have been allocated. */
      if (!alloc_success)
        {
          while (i-- != 0)
            {
              free_map_release (indices[i], 1);
            }
          free (indices);
          free (disk_inode);
          return alloc_success;
        }

      /* Assign all possible sector indices to the direct block of the disk_inode and write them to the disk. */
      int direct_max = (sectors < NUM_DIRECT_BLOCKS) ? sectors : NUM_DIRECT_BLOCKS;
      int indirect_max = ((sectors - NUM_DIRECT_BLOCKS) < INDIRECT_BLOCK_SECTORS) ? (sectors - NUM_DIRECT_BLOCKS) : INDIRECT_BLOCK_SECTORS;
      int double_indirect_max = sectors - (NUM_DIRECT_BLOCKS + INDIRECT_BLOCK_SECTORS);

      /* If any of the assignment fails free resources and return false. */
      if (!init_disk_inode_direct (inode, indices, direct_max) || !init_disk_inode_indirect (inode, indices + direct_max, indirect_max)
        || init_disk_inode_double_indirect (inode, indices + direct_max + indirect_max, double_indirect_max))
      {
        free (indices);
        free (disk_inode);
        return success;
      }

      success = true;
      static char zeros[BLOCK_SECTOR_SIZE];
      /* Write the inode_disk to the disk. */
      block_write (fs_device, sector, disk_inode);
      /* Initialize the data of the file with zeros. */
      struct inode * opened_inode = inode_open (sector);
      int offset = 0;
      for (i = 0; i < sectors; ++i, offset += BLOCK_SECTOR_SIZE)
      {
        inode_write_at (opened_inode, zeros, BLOCK_SECTOR_SIZE, offset);
      }
      inode_close (opened_inode);
      free (indices);
      free (disk_inode);
    }
  return success;
}

static void
init_disk_inode_direct (struct inode_disk *inode, block_sector_t *indices, int limit)
{
  int i;
  for (i = 0; i < limit; ++i)
    {
      inode->direct[i] = indices[i];
    }
}

static bool
init_disk_inode_indirect (struct inode_disk *inode, block_sector_t *indices, int limit)
{
  if (!init_indirect (indices, limit, &(inode->indirect)))
    return false;
  else
    return true;
}

static bool
init_disk_inode_double_indirect (struct inode_disk *inode, block_sector_t *indices, int limit)
{
  block_sector_t sector_index;
  
  /* Allocate as many double indirect blocks as we will need */
  bool allocate_success = true;
  unsigned double_indirect_blocks_needed = DIV_ROUND_UP (limit, INDIRECT_BLOCK_SECTORS);
  block_sector *indirect_indices = (block_sector*) malloc (sizeof (block_sector) * double_indirect_blocks_needed);

  /* Allocate memory for all the indirect blocks we need and asssign the indices of the blocks of the file to those indirect blocks. */
  int i;
  int double_indices_limit = INDIRECT_BLOCK_SECTORS;
  for (i = 0, double_indirect_block_index = 0; i < double_indirect_blocks_needed; ++i)
    {
      /* If the is the last block to be allocated, calculate the number of sector needed to be allocated for the last block,
          otherwise assign just assign 128 (INDIRECT_BLOCK_SECTORS) sector indices*/
      if ((double_indirect_block_index - i) == 1)
      {
        double_indices_limit = limit - (i * INDIRECT_BLOCK_SECTORS);
      }
      /* Allocate the second level indirect block and assign the sector indices of the file to the block. */
      if (!init_indirect ((indices + (i * INDIRECT_BLOCK_SECTORS)), double_indices_limit, &(indirect_indices[i])))
        {
          allocate_success = false;
          break;
        }
      indirect_indices[i] = sector_index;
    }

  /* If we failed to allocate completely, free the allocations */
  if (!allocate_success)
    {
    while (i-- != 0)
      {
        free_map_release(indirect_indices[i], 1);
      }
    free (indirect_indices);
    return allocate_success;
    }

  /* Assign the indices of the newly created indirect blocks to the double indirect block of the disk_inode */
  if (!init_indirect (indirect_indices, double_indirect_blocks_needed, &(inode->double_indirect)))
    {
      free (indirect_indices);
      return allocate_success;
    }

  free (indirect_indices);
  return allocate_success;
}

static bool
init_indirect (block_sector_t *indices, int limit, block_sector_t *sector_index)
{
  int i;
  struct indirect_block *indirect = (struct indirect_block *) malloc (sizeof struct indirect_block);

  if (indirect == NULL)
    return false;
  for (i = 0; i < limit; ++i)
    {
      indirect->direct[i] = indices[i];
    }
  if (!free_map_allocate (1, sector_index))
    return false;
  block_write (fs_device, *sector_index, indirect);
  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* Calculate the number of bytes to expand. */
  size_t sectors_data_allocate_successfully = 0;
  size_t sectors_grow_successfully = 0;
  unsigned bytes_to_allocate = (offset + size - inode.data.length);
  if (bytes_to_allocate > 0)
    {
      /* Number of sectors to expand. */
      size_t sectors = bytes_to_sectors (bytes_to_allocate);

      block_sector_t *indices = malloc (sectors * sizeof (block_sector_t));

      ASSERT (indices != NULL);

      /* Allocate sectors for the file data. */
      int i, sector_index;
      for (i = 0; i < sectors; ++i)
        {
          if (!free_map_allocate (1, &sector_index))
            {
              break;
            }
            ++sectors_allocate_successfully;
            indices[i] = sector_index;
        }

      /* Get as much as sectors that successfully get allocated. */
      sectors = sectors_data_allocate_successfully;

      /* Find the number of sectors in the file so far. */
      off_t current_num_sectors = DIV_ROUND_UP (inode->data.length, BLOCK_SECTOR_SIZE); 

      /* Find the number of blocks needed for the direct block (direct_limit), the indirect block (indirect_limit) and 
      the double indirect block (double_indirect_limit) and their corresponding starting offset. */
      int direct_limit = 0;
      int indirect_limit = 0;
      int double_indirect_limit = 0;
      /* If the growth starts from the direct blocks. */
      if (current_num_sectors < NUM_DIRECT_BLOCKS)
        {
          direct_limit = (sectors < (NUM_DIRECT_BLOCKS - current_num_sectors)) ? sectors : (NUM_DIRECT_BLOCKS - current_num_sectors);
          sectors_grow_successfully += direct_limit;
          assign_inode_direct (inode, indices, direct_limit, current_num_sectors);
          if (direct_limit != sectors)
            {
              indirect_limit = ((sectors - direct_limit) < INDIRECT_BLOCK_SECTORS) ? (sectors - direct_limit) : INDIRECT_BLOCK_SECTORS;
              sectors_grow_successfully += indirect_limit;
              assign_inode_indirect (inode, indices + direct_limit, indirect_limit, 0);
              if (indirect_limit != (sectors - direct_limit))
                {
                  double_indirect_limit = sectors - indirect_limit - direct_limit;
                  sectors_grow_successfully += assign_inode_double_indirect (inode, indices + direct_limit + indirect_limit, double_indirect_limit, 0);
                }
            }
        }
      /* If the growth starts from the indirect blocks. */
      else if (current_num_sectors < (NUM_DIRECT_BLOCKS + INDIRECT_BLOCK_SECTORS))
        {
          indirect_limit = (sectors < (INDIRECT_BLOCK_SECTORS - (current_num_sectors - NUM_DIRECT_BLOCKS))) ? (sectors) 
          : (INDIRECT_BLOCK_SECTORS - (current_num_sectors - NUM_DIRECT_BLOCKS));
          sectors_grow_successfully += indirect_limit;
          assign_inode_indirect (inode, indices, indirect_limit, 0);
          if (indirect_limit != sectors)
            {
              double_indirect_limit = sectors - indirect_limit;
              sectors_grow_successfully += assign_inode_double_indirect (inode, indices + indirect_limit, double_indirect_limit, 0);
            }
        }
      /* If the growth starts from the second level indirect blocks. */
      else
        {
          double_indirect_limit = sectors;
          sectors_grow_successfully += assign_inode_double_indirect (inode, indices, double_indirect_limit, 0);
        }

      /* Update the length of the file. */
      inode->data.length += (sectors_grow_successfully < bytes_to_sectors (bytes_to_allocate)) ?
                              : size;
      free (indices);
    }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

static void
assign_inode_direct (struct inode *inode, block_sector_t *indices, int limit, off_t offset)
{
  int i;
  for (i = offset; i < limit; ++i)
    {
      inode->direct[i] = indices[i];
    }
}

static void
assign_inode_indirect (struct inode *inode, block_sector_t *indices, int limit, off_t offset)
{

  struct indirect_block *indirect = (struct indirect_block *) malloc (sizeof struct indirect_block);

  ASSERT (indirect != NULL);

  block_read (fs_device, inode->data.indirect, indirect);

  int i;
  for (i = offset; i < limit; ++i)
    {
      (indirect->direct)[i] = indices[i];
    }

  block_write (fs_device, inode->data.indirect, indirect);
  free (indirect);
}

static size_t
assign_inode_double_indirect (struct inode *inode, block_sector_t *indices, int limit, off_t offset)
{
  block_sector_t sector_index;
  
  /* Allocate as many double indirect blocks as we will need */
  size_t num_blocks_allocate_success = 0;
  unsigned double_indirect_blocks_needed = DIV_ROUND_UP (limit, INDIRECT_BLOCK_SECTORS);
  block_sector *indirect_indices = (block_sector*) malloc (sizeof (block_sector) * double_indirect_blocks_needed);

  ASSERT (indirect_indices != NULL);

  /* Allocate memory for all the indirect blocks we need and asssign the indices of the blocks of the file to those indirect blocks. */
  int i;
  int double_indices_limit = INDIRECT_BLOCK_SECTORS;
  for (i = 0, double_indirect_block_index = 0; i < double_indirect_blocks_needed; ++i)
    {
      /* If the is the last block to be allocated, calculate the number of sector needed to be allocated for the last block,
          otherwise assign just assign 128 (INDIRECT_BLOCK_SECTORS) sector indices*/
      if ((double_indirect_block_index - i) == 1)
      {
        double_indices_limit = limit - (i * INDIRECT_BLOCK_SECTORS);
      }
      /* Allocate the second level indirect block and assign the sector indices of the file to the block. */
      if (!init_indirect ((indices + (i * INDIRECT_BLOCK_SECTORS)), double_indices_limit, &(indirect_indices[i])))
        {
          break;
        }
      ++num_blocks_allocate_success;
      indirect_indices[i] = sector_index;
    }

  struct indirect_block *indirect = (struct indirect_block *) malloc (sizeof struct indirect_block);

  ASSERT (indirect != NULL);

  block_read (fs_device, inode->data.indirect, indirect);

  int i;
  for (i = offset; i < limit; ++i)
    {
      (indirect->direct)[i] = indices[i];
    }

  block_write (fs_device, inode->data.double_indirect, indirect);

  free (indirect);
  free (indirect_indices);

  return num_blocks_allocate_success;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
