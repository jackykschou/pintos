#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

// static unsigned inode_grow(struct inode_disk *disk_inode, unsigned sectors_to_grow, bool must_succeed);
// static void inode_map_sector_index(struct inode_disk *disk_inode, unsigned block_index, unsigned sector_number);
// static void direct_map_index(struct inode_disk *disk_inode, unsigned block_index, unsigned sector_number);
// static void indirect_map_index(struct inode_disk *disk_inode, unsigned block_index, unsigned sector_number);
// static void double_indirect_map_index(struct inode_disk *disk_inode, unsigned block_index, unsigned sector_number);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

static void
direct_map_index (struct inode_disk *disk_inode, size_t block_index, size_t sector_number)
{
  
  disk_inode->direct[block_index] = sector_number;
}

static void
indirect_map_index (struct inode_disk *disk_inode, size_t block_index, size_t sector_number)
{
  /* Find the index if the indirect blocks direct block array */
  size_t relative_index = block_index - MAX_INDEX_DIRECT;

  struct indirect_block* ptr = malloc (sizeof (struct indirect_block));
  block_read (fs_device, disk_inode->indirect, ptr);

  
  ptr->direct[relative_index] = sector_number;

  block_write (fs_device, disk_inode->indirect, ptr);
  free (ptr);

}

static void 
double_indirect_map_index (struct inode_disk *disk_inode, size_t block_index, size_t sector_number)
{
  /*Find the relative index */
  size_t relative_index = block_index - MAX_INDEX_INDIRECT;

  /*Determine which second level block to access - and where the data will go in that*/
  size_t second_level_block_index = relative_index / INDIRECT_BLOCK_SECTORS;
  size_t second_level_relative_index = relative_index % INDIRECT_BLOCK_SECTORS;

  /* Get first level block data */
  struct indirect_block* first_level_ptr = malloc (sizeof (struct indirect_block));
  block_read (fs_device, disk_inode->double_indirect, first_level_ptr);

  /* Get second level block data */
  struct indirect_block* second_level_ptr = malloc (sizeof(struct indirect_block));
  block_read (fs_device, first_level_ptr->direct[second_level_block_index], second_level_ptr);

  /* Write index */
  second_level_ptr->direct[second_level_relative_index] = sector_number;

  /* Write to disk and free recourses */
  block_write (fs_device, first_level_ptr->direct[second_level_block_index], second_level_ptr);
  block_write (fs_device, disk_inode->double_indirect, first_level_ptr);
  free (first_level_ptr);
  free (second_level_ptr);
}

static void 
inode_map_sector_index (struct inode_disk *disk_inode, size_t block_index, size_t sector_number)
{
  if (block_index < MAX_INDEX_DIRECT)
    direct_map_index(disk_inode, block_index, sector_number);
  else if (block_index < MAX_INDEX_INDIRECT)
    indirect_map_index(disk_inode, block_index, sector_number);
  else if (block_index < MAX_INDEX_DOUBLE_INDIRECT)
    double_indirect_map_index(disk_inode, block_index, sector_number);
}


static size_t
get_direct_map_index (struct inode_disk *disk_inode, size_t block_index)
{
  
  // 
  return disk_inode->direct[block_index];
}

static size_t 
get_indirect_map_index (struct inode_disk *disk_inode, size_t block_index)
{
  /* Find the index if the indirect blocks direct block array */
  size_t relative_index = block_index - MAX_INDEX_DIRECT;

  struct indirect_block* ptr = malloc (sizeof (struct indirect_block));
  block_read (fs_device, disk_inode->indirect, ptr);

  size_t result = ptr->direct[relative_index];

  free (ptr);

  return result;
}

static size_t 
get_double_indirect_map_index (struct inode_disk *disk_inode, size_t block_index)
{
  /*Find the relative index */
  size_t relative_index = block_index - MAX_INDEX_INDIRECT;

  /*Determine which second level block to access - and where the data will go in that*/
  size_t second_level_block_index = relative_index / INDIRECT_BLOCK_SECTORS;
  size_t second_level_relative_index = relative_index % INDIRECT_BLOCK_SECTORS;

  /* Get first level block data */
  struct indirect_block* first_level_ptr = malloc (sizeof (struct indirect_block));
  block_read (fs_device, disk_inode->double_indirect, first_level_ptr);

  /* Get second level block data */
  struct indirect_block* second_level_ptr = malloc (sizeof(struct indirect_block));
  block_read (fs_device, first_level_ptr->direct[second_level_block_index], second_level_ptr);

  /* Write index */
  size_t result = second_level_ptr->direct[second_level_relative_index];

  /* Write to disk and free recourses */
  free (first_level_ptr);
  free (second_level_ptr);

  return result;
}

static block_sector_t 
get_inode_map_sector_index (struct inode_disk *disk_inode, size_t block_index)
{
  size_t result;
  if (block_index < MAX_INDEX_DIRECT)
    result = get_direct_map_index(disk_inode, block_index);
  else if (block_index < MAX_INDEX_INDIRECT)
   result = get_indirect_map_index(disk_inode, block_index);
  else if (block_index < MAX_INDEX_DOUBLE_INDIRECT)
    result = get_double_indirect_map_index(disk_inode, block_index);
  else
    result = -1;

  return result;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  /* Check invalid */
  if (pos >= inode->data.length || pos < 0)
    return -1;

  block_sector_t sector_offset = pos / BLOCK_SECTOR_SIZE;
  
  return get_inode_map_sector_index(&inode->data, sector_offset);
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

static size_t
inode_grow(struct inode_disk *disk_inode, size_t sectors_to_grow, bool must_succeed)
{
  if (sectors_to_grow <= 0)
    {
      return 0;
    }

  static char zeros[BLOCK_SECTOR_SIZE];
  size_t sectors_successfully_allocated = 0;
  bool alloc_success = false;
  size_t starting_sectors = bytes_to_sectors (disk_inode->length);
  size_t desired_ending_sectors = starting_sectors + sectors_to_grow; 

  int i;
  int sector_index;

  /* Check how large the inode is trying to grow */

  /* Allocate sectors for the file data. - we need enough for the data as well as the index blocks
    we must calculate how many NEW index blocks are needed after we find out how many data blocks we can actually allocate*/

  /* Allocate the data blocks required */
  
  

  block_sector_t *allocated_sectors = malloc (sectors_to_grow * sizeof (block_sector_t));
  for (i = 0; i < sectors_to_grow; ++i)
  {
    /* Allocate this sector - if we fail, determine behavior based on must_succeed */
    if (!free_map_allocate (1, &allocated_sectors[i]))
      {
        
        /* If we must succeed and failed, we will return -1 */
        if (must_succeed == true)
        {
          sectors_successfully_allocated = -1;
          alloc_success = false;
          goto return_result;   
        }

        else
        {
          sectors_successfully_allocated = i;
          break;
        }
      }
      
      /* If we didn't fail */
      sectors_successfully_allocated++;
  }

  /* Initialize the data to zero. */
  for (i = 0; i < sectors_successfully_allocated; ++i)
  {
    block_write (fs_device, allocated_sectors[i], zeros);
  }

  /* Determine how many indirect blocks we need - given how many we actually allocated */
  size_t actual_ending_sectors = starting_sectors + sectors_successfully_allocated;

  bool need_indirect_block = ((starting_sectors < MAX_INDEX_DIRECT) && (actual_ending_sectors > MAX_INDEX_DIRECT)) ? true : false;
  bool need_first_level_block = ((starting_sectors < MAX_INDEX_INDIRECT) && (actual_ending_sectors > MAX_INDEX_INDIRECT) ) ? true : false;

  int starting_second_level_blocks = ((starting_sectors - MAX_INDEX_INDIRECT) / INDIRECT_BLOCK_SECTORS);
  int ending_second_level_blocks = ((actual_ending_sectors - MAX_INDEX_INDIRECT) / INDIRECT_BLOCK_SECTORS);

  bool need_second_level_block = true;

  if (((int)(starting_sectors - MAX_INDEX_INDIRECT)) < 0)
  {
    need_second_level_block = false;
  }

  size_t second_level_blocks_to_allocate = ending_second_level_blocks - starting_second_level_blocks;

  /* Allocate index blocks required - if we fail any of these, we should quit*/
  size_t indirect_block_index;
  size_t first_level_block_index;

  block_sector_t *allocated_second_level_blocks = malloc (second_level_blocks_to_allocate * sizeof (block_sector_t));

  /* If we now have enough to require a indirect block */
  if (need_indirect_block)
  {
    if (!free_map_allocate (1, &indirect_block_index))
    {
      sectors_successfully_allocated = -1;
      alloc_success = false;
      goto return_result;
    }

    /* Initialize indirect block */
    disk_inode->indirect = indirect_block_index;
    struct indirect_block *indirect_ptr = malloc(sizeof(struct indirect_block));

    block_write (fs_device, disk_inode->indirect, indirect_ptr);
    free (indirect_ptr);
  }

  /* If we now have enough for a double indirect block */
  if (need_first_level_block)
  {
    if (!free_map_allocate (1, &first_level_block_index))
    {
      sectors_successfully_allocated = -1;
      alloc_success = false;
      goto return_result;
    }

    /* Initialize double indirect block */
    disk_inode->double_indirect = first_level_block_index;
    struct indirect_block *double_indirect_first_level_ptr = malloc (sizeof(struct indirect_block));
    block_write (fs_device, disk_inode->double_indirect, double_indirect_first_level_ptr);
    free (double_indirect_first_level_ptr);
  }

  if (need_second_level_block)
  {
    /* Create all the new second level blocks we need */
    for (i = 0; i < second_level_blocks_to_allocate; ++i)
    {
      if (!free_map_allocate (1, &allocated_second_level_blocks[i]))
      {
        sectors_successfully_allocated = -1;
        alloc_success = false;
        goto return_result;
      }
    }

    /* Map all second level indirect blocks in the first level indirect block */

    /* Load the double indirect first level block array from disk */ 
    struct indirect_block* double_indirect_first_level_ptr = malloc (sizeof (struct indirect_block));
    block_read (fs_device, disk_inode->double_indirect, double_indirect_first_level_ptr);

    int k;
    for (i = starting_second_level_blocks, k = 0; i < ending_second_level_blocks; ++i, ++k)
    {
      double_indirect_first_level_ptr->direct[i] = allocated_second_level_blocks[k];
    }

    block_write (fs_device, disk_inode->double_indirect, double_indirect_first_level_ptr);
    free (double_indirect_first_level_ptr);

  }

  /* Add the references to allocated blocks in disk_inode's direct/index blocks 
  start at the sector after the last pre-existing one */
  int j;
  for (i = starting_sectors, j = 0; i < actual_ending_sectors; ++i, ++j)
  {
    
    inode_map_sector_index (disk_inode, i, allocated_sectors[j]);
  }

  alloc_success = true;

  /* Free recourses and return */
  return_result:

  /*If for any reason the allocation failed, free all allocated sectors */
  if (!alloc_success)
  {
    /* Free the index blocks */
    free_map_release(indirect_block_index, 1);
    free_map_release(first_level_block_index, 1);

    for (i = 0; i < second_level_blocks_to_allocate; ++i)
    {
      free_map_release(allocated_second_level_blocks[i], 1);
    }

    /* Free the data blocks */
    for(i = 0; i < sectors_successfully_allocated; ++i)
    {
      free_map_release (allocated_sectors[i], 1);
    }
  }
  
  /* Always free the temporary arrays */
  free (allocated_second_level_blocks);
  free (allocated_sectors);

  return sectors_successfully_allocated;

}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{

  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  size_t sectors = bytes_to_sectors (length);
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    //TODO how do we allocate the disk_inode?

    /* Put data in the disk inode */
    disk_inode->length = 0;
    disk_inode->is_dir = is_dir;
    disk_inode->magic = INODE_MAGIC;
    
    /*Grow the file to the size specified - fills new sectors with zero's and maps all indicies */
    size_t result = inode_grow (disk_inode, sectors, true);

    /* Allocation failed - free resources*/
    if (result == -1)
    {
      goto return_result;
    }    

    disk_inode->length = length;
    /* File is successfully created if we made it through */
    success = true;

    /* Write the disk_node back to the disk. */
    block_write (fs_device, sector, disk_inode);
  }

  return_result:

  free (disk_inode);

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

          int i = 0;
          block_sector_t sector;
          if ((sector = byte_to_sector (inode, i)) != -1)
            {
              i += BLOCK_SECTOR_SIZE;
              free_map_release (sector, 1);
            }
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

/* Helper function to determine how many sectors a file needs to grow have size bytes written at offset */
static size_t 
sectors_needed_to_grow (size_t file_len, off_t offset, off_t size)
{

  size_t allocated_sectors = file_len / BLOCK_SECTOR_SIZE + 1;

  if(file_len == 0)
  {
    allocated_sectors = 0;
  }

  size_t allocated_bytes = allocated_sectors * BLOCK_SECTOR_SIZE;

  int bytes_to_grow = (size + offset) - allocated_bytes;

  int sectors_to_grow = 0;

  if (bytes_to_grow > 0)
  {
    sectors_to_grow = bytes_to_sectors(bytes_to_grow);
  }

  return (sectors_to_grow > 0) ? sectors_to_grow : 0; 
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

  /* Do we need to grow the file */
  size_t sectors_to_grow = sectors_needed_to_grow (inode_length(inode), offset, size);

  size_t sectors_grown = inode_grow (&inode->data, sectors_to_grow, false);

  if (sectors_grown < sectors_to_grow)
    {
      inode->data.length += (sectors_grown * BLOCK_SECTOR_SIZE);
    }
  else
    {
      if (inode->data.length < (offset + size))
        {
          inode->data.length = offset + size;
        }
    }

  block_write (fs_device, inode->sector, &inode->data);

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