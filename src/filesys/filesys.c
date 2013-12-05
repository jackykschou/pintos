#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"

#define MAX_DIR_DEPTH 10

/* Partition that contains the file system. */
struct block *fs_device;

struct inode;
struct inode_disk;

static void do_format (void);
static bool parse_path (const char *name, struct dir **dir, char *parsed_name);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{

  block_sector_t inode_sector = 0;
  struct dir *dir;
  char parsed_name[NAME_MAX + 1];
  
  if (*name == NULL || (strlen (name) > NAME_MAX))
    {
      return false;
    }
  bool success = parse_path (name, &dir, parsed_name);
  if (!success)
    {
      return success;
    }

  success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, parsed_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);


  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir;
  struct inode *inode = NULL;
  char parsed_name[NAME_MAX + 1];
  
  if (*name == NULL || (strlen (name) > NAME_MAX))
    {
      return NULL;
    }
  
  bool success = parse_path (name, &dir, parsed_name);

  if (success)
  {
    if (dir != NULL)
      dir_lookup (dir, parsed_name, &inode);
    dir_close (dir);
  }

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir;
  char parsed_name[NAME_MAX + 1];
  
  if (*name == NULL || (strlen (name) > NAME_MAX))
    {
      return false;
    }
  
  bool success = parse_path (name, &dir, parsed_name);

  if (!success)
    {
      return success;
    }

  success = dir != NULL && dir_remove (dir, parsed_name);
  dir_close (dir); 

  return success;
}

/* UNFINISHED */
/* Creates directory. True if successful. */
bool 
filesys_mkdir (const char *dir)
{
  bool success = false;

  /*The string *dir cannot be empty.*/
  if (*dir == NULL)
    {
      return success;
    }

  /* Locates the directory where the new directory should be created. */
  struct dir *create_dir;
  char parsed_name[NAME_MAX + 1];
  parse_path (dir, &create_dir, parsed_name);

  block_sector_t sector;
  /*Find a free sector for the directory.*/
  if (!free_map_allocate (1, &sector))
    {
      return success;
    }

  success = dir_create (sector, 16);
  if (!success)
    {
      free_map_release (sector, 1);
      return success;
    }

  success = dir_add (create_dir, parsed_name, sector);
  if (!success)
    {
      free_map_release (sector, 1);
      return success;
    }

  /* Get the dir struct of the directory that is just created and add "." and "..". */
  struct inode *inode = inode_open (sector);
  struct dir *new_dir = dir_open (inode);

  ASSERT (inode != NULL);
  ASSERT (new_dir != NULL);

  dir_add (new_dir, ".", sector);
  dir_add (new_dir, "..", create_dir->inode->sector);
  dir_close (new_dir);
  dir_close (create_dir);

  return success; 
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();

  struct dir *root = dir_open_root ();
  dir_add (root, ".", ROOT_DIR_SECTOR);
  dir_add (root, "..", ROOT_DIR_SECTOR);
  dir_close (root);

  printf ("done.\n");
}

/* Parse an absolute or relative file path and find the directory and the file name the put it to the buffers (dir and parsed_name) */
static bool
parse_path (const char *name, struct dir **dir, char *parsed_name)
{

  // *dir = dir_open_root();
  // strlcpy (parsed_name, name, strlen (name) + 1);
  // return true;

  bool success = true;
  struct dir *cur_dir;
  /* If it is absolute path. */
  if (name[0] == '/')
    {
      cur_dir = dir_open_root ();
    }
  /* If it is relative path. */
  else
    {
      cur_dir = dir_open (inode_open (thread_current()->cur_dir_sector));
    }

  /* Search through the directories to get the desired directory and the name of the file. */
  struct inode *inode = NULL;
  char *token, *save_ptr;
  char *name_copy = (char*) malloc (strlen (name));
  strlcpy (name_copy, name, strlen (name) + 1);
  if (name_copy == NULL)
    {
      success = false;
      goto return_result;
    }

  /* Initialize an array to stores the different file names of the path. */
  char **dirs = (char**)malloc (MAX_DIR_DEPTH * sizeof(char*));
  int i;
  for (i = 0; i < MAX_DIR_DEPTH; ++i)
    {
      dirs[i] = (char*) malloc ((NAME_MAX + 1) * sizeof(char*));
    }
  /* Read all the file names of the path. */
  for (token = strtok_r (name_copy, "/", &save_ptr), i = 0; token != NULL; token = strtok_r (NULL, "/", &save_ptr), ++i)
    {
      strlcpy (dirs[i], token, strlen (token) + 1);
    }
  /* Get the last file name as the parsed name. */
  strlcpy (parsed_name, dirs[i - 1], strlen (dirs[i - 1]) + 1);

  /* Get the directory where the file (parsed name) is located. */
  int dir_num = i - 1;
  for (i = 0; i < dir_num; ++i)
    {
      success = dir_lookup (cur_dir, dirs[i], &inode);
      if (!success)
        {
          goto return_result;
        }
      if (!(inode->data).is_dir)
        {
          success = false;
          goto return_result;
        }
      dir_close (cur_dir);
      cur_dir = dir_open (inode);
      if (cur_dir == NULL)
        {
          success = false;
          goto return_result;
        }
    }
  return_result:

  if (!success)
  {
    dir_close (cur_dir);
  }
  for (i = 0; i < MAX_DIR_DEPTH; ++i)
    {
      free (dirs[i]);
    }
  free (name_copy);
  free (dirs);

  *dir = cur_dir;

  return success;
}
