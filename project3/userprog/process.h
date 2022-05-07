#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#define MAX_NUMBER_OF_FILES_IN_PROCESS 50

typedef int tid_t;



typedef struct process_control_block{
    tid_t tid;
    tid_t parent_tid;
    int child_exec_fail;

    //Will contain the number of open files. 0 will mean that index 0 1 are used (stdin/out)
    struct file* file_descriptor_table[MAX_NUMBER_OF_FILES_IN_PROCESS];
    int number_open_files;
} process_control_block;

tid_t process_execute(const char *file_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(void);

#endif /* userprog/process.h */
