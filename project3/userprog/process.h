#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

typedef int tid_t;

typedef struct process_control_block{
    tid_t tid;
    tid_t parent_tid;
    struct file* file_descriptor_table[50];
} process_control_block;

tid_t process_execute(const char *file_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(void);

#endif /* userprog/process.h */
