#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of direct sectors in the inode.*/
#define NUM_DIRECT_BLOCKS 124

/* Number of sectors that in the first level block of the inode. */
#define INDIRECT_BLOCK_SECTORS (BLOCK_SECTOR_SIZE / sizeof (block_sector_t))

/* Number of direct sectors in the inode.*/
#define MAX_INDEX_DIRECT (NUM_DIRECT_BLOCKS)

/* Total sum of number of sectors that in the direct and the first level block of the inode. */
#define MAX_INDEX_INDIRECT (MAX_INDEX_DIRECT + INDIRECT_BLOCK_SECTORS)

/* Total sum of number of sectors that in the direct, the first level, and the second level block of the inode. */
#define MAX_INDEX_DOUBLE_INDIRECT (MAX_INDEX_INDIRECT + INDIRECT_BLOCK_SECTORS * INDIRECT_BLOCK_SECTORS)

struct bitmap;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t direct[NUM_DIRECT_BLOCKS];     /* Indicies for direct data blocks */
    block_sector_t indirect;                      /* Index to first-level index block */
    block_sector_t double_indirect;               /* Index to second-level index block */
    off_t length;                                 /* File size in bytes. */
    bool is_dir;                                  /* If the file is a directory. */
  };

/* Indirect block in the multi-level inode. */
struct indirect_block
  {
    block_sector_t direct[INDIRECT_BLOCK_SECTORS];	/* Sector indeices for data of the indirect block. */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock grow_lock;              /* Lock for file grow operation. */
    struct lock dir_lock;               /* Lock for directory operation. */
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
