#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* File descriptor operations */
struct file *get_file_struct (int fd);
int add_file_descriptor (struct file *file);
void remove_file_descriptor (int fd);

void stack_grow ();

#endif /* userprog/process.h */
