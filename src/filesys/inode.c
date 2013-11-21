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

  /* If pos in within direct block */
  if (pos < NUM_DIRECT_BYTES)
  {
    return inode->data.direct[pos / BLOCK_SECTOR_SIZE];
  }

  block_sector_t sector_offset = pos / BLOCK_SECTOR_SIZE;

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
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      /* Allocate sectors for the file data. */
      int i, index;
      for (i = 0; i < sectors; ++i)
        {
          if (!free_map_allocate (1, &index))
            {
              alloc_success = false
              break;
            }
            indices[i] = index;
        }
      /* If allocation fails, deallocate all the sectors that have been allocated. */
      if (!alloc_success)
        {
          while (i-- != 0)
            {
              free_map_release (indices[i], 1);
            }
          free (indices);
          return alloc_success;
        }

      /* Load all possible sectors index to the direct block of the disk_inode and write them to the disk. */
      int k;
      static char zeros[BLOCK_SECTOR_SIZE];
      int direct_max = (sectors < NUM_DIRECT_BLOCKS) ? sectors : NUM_DIRECT_BLOCKS;
      for (k = 0; k < direct_max; ++k)
        {
          disk_inode->direct[k] = indices[k];

        }

      /* Load all possible sectors index to the first level indirect direct block of the disk_inode. */
      if ((sectors - NUM_DIRECT_BLOCKS) > 0)
        {
          int indirect_max = (sectors - NUM_DIRECT_BLOCKS < NUM_DIRECT_BLOCKS) ? sectors : INDIRECT_BLOCK_SECTORS;
          for (k = 0; k < indirect_max; ++k)
            {
              disk_inode->indirect[k] = indices[k + NUM_DIRECT_BLOCKS];
              block_write (fs_device, indices[k + NUM_DIRECT_BLOCKS], zeros);
            }

        }
      if (sectors > NUM_DIRECT_BLOCKS + INDIRECT_BLOCK_SECTORS)
        {

        }



      success = true;
      block_write (fs_device, sector, disk_inode);
      if (sectors > 0) 
        {
          static char zeros[BLOCK_SECTOR_SIZE];


          size_t j;
          
          for (j = 0; j < sectors; j++) 
            block_write (fs_device, disk_inode->start + j, zeros);
        }
      free (indices);
      free (disk_inode);
    }
  return success;
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
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

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
