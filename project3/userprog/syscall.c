#include <stdio.h>
#include <syscall-nr.h>

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

#define DEBUG 1

#ifdef DEBUG
#define _DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define _DEBUG_PRINTF(...) /* do nothing */
#endif

static void syscall_handler(struct intr_frame *);
static struct lock file_lock;
static void aquire_fs_lock(void);
static void release_fs_lock(void);
static int valid_pointer(void* provided_pointer);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);

void sys_halt (void);
void sys_exit(int status);
int sys_exec (const char *cmd_line);
int sys_wait(tid_t tid);
int sys_write (int fd, char *buffer, unsigned size);
int sys_open(const char *file);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove(const char *file);

void syscall_init(void)
{
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
    lock_init(&file_lock);
}

static void
syscall_handler(struct intr_frame *f UNUSED)
{
    //Make sure stack pointer is valid
    if(!valid_pointer(f->esp)){
         sys_exit(-1);
    }
    //Before using the args, will need to check they are all below 0xC0000000
    uint32_t *usp = f->esp;
    int call_no = *usp;
    int arg0 = *(usp+1);
    int arg1 = *(usp+2);;
    int arg2 = *(usp+3);;
    int arg3 = *(usp+4);;

    switch(call_no) {
        case SYS_HALT:
            sys_halt();
            break;
        case SYS_EXIT:
            if(!valid_arg(usp+1)){
                sys_exit(-1);
            }
            sys_exit(arg0);
            break;
        case SYS_EXEC:
            if(!valid_pointer(arg0) || !valid_arg(usp+1)){
                sys_exit(-1);
            }
            f->eax = sys_exec((char*) arg0);
            break;
        case SYS_WAIT:
            break;
        case SYS_CREATE:
            if(!valid_pointer(arg0) || !valid_arg(usp+2)){
                sys_exit(-1);
            }
            f->eax = sys_create((char *) arg0, (int) arg1);
            break;
        case SYS_REMOVE:
            if(!valid_pointer(arg0) || !valid_arg(usp+1)){
                sys_exit(-1);
            }
            f->eax = sys_remove((char *) arg0);
            break;
        case SYS_OPEN:
            if(!valid_pointer(arg0) || !valid_arg(usp+1)){
                sys_exit(-1);
            }
            f->eax = sys_open((char*) arg0);
            break;
        case SYS_FILESIZE:
            if(!valid_arg(usp+1)){
                sys_exit(-1);
            }
            f->eax = sys_filesize((int) arg0);
            break;
        case SYS_READ:
            if(!valid_pointer(arg1) || !valid_arg(usp+3)){
                sys_exit(-1);
            }
            f->eax = sys_read((int) arg0, (char*) arg1, (unsigned) arg2);
            break;
        case SYS_WRITE:
            if(!valid_pointer(arg1) || !valid_arg(usp+3)){
                sys_exit(-1);
            }
            f->eax = sys_write((int) arg0, (char*) arg1, (unsigned) arg2);
            break;
        case SYS_SEEK:
            break;
        case SYS_TELL:
            break;
        case SYS_CLOSE:
            break;
    }
}

void sys_halt (void){
    /*
    System Call: void halt (void)
        Terminates Pintos by calling shutdown_power_off() (declared in "devices/shutdown.h").
        This should be seldom used, because you lose some information about possible deadlock situations, etc. 
    */

    shutdown_power_off();
}

void sys_exit(int status){
    /*
    System Call: void exit (int status)
        Terminates the current user program, returning status to the kernel.
        If the process's parent waits for it (see below), this is the status that will be returned.
        Conventionally, a status of 0 indicates success and nonzero values indicate errors. 
    */

    struct thread *cur = thread_current();
    struct thread *par;
    printf("%s: exit(%d)\n", cur->name, status);
    cur->exit_status = status;

    //If you have a parent thread from exec, wake them up!
    if(cur->pcb.parent_tid != 1){
        get_thread_tcb(cur->pcb.parent_tid, &par);
        sema_up(&(par->exec_wait_on_child));
    } else { //You have the kernel waiting on you
        //Wake up the parent to get status
        sema_up(&cur->exit_block_on_parent);
        //Wait till parents gets status to cleanup
        sema_down(&cur->exit_block_on_child);
    }   
        
    thread_exit();
}

int sys_exec (const char *cmd_line){
    /*
    System Call: pid_t exec (const char *cmd_line)

        Runs the executable whose name is given in cmd_line, passing any given arguments, and returns
        the new process's program id (pid). Must return pid -1, which otherwise should not be a valid pid,
        if the program cannot load or run for any reason. Thus, the parent process cannot return from the
        exec until it knows whether the child process successfully loaded its executable. You must use appropriate
        synchronization to ensure this.
    */
   tid_t process_tid = -1;
   if(cmd_line == NULL){
       return -1;
   }
   process_tid = process_execute(cmd_line);
   //Wait on the child process to down your semaphore
   sema_down(&thread_current()->exec_wait_on_child);
   if(thread_current()->pcb.child_exec_fail == 1){
       return -1;
   }
   return process_tid;

}

int sys_wait(tid_t tid){
    /*
    System Call: int wait (pid_t pid)
        Waits for a child process pid and retrieves the child's exit status.

        If pid is still alive, waits until it terminates. Then, returns the status that pid passed to exit.
        If pid did not call exit(), but was terminated by the kernel (e.g. killed due to an exception), wait(pid)
        must return -1. It is perfectly legal for a parent process to wait for child processes that have already
        terminated by the time the parent calls wait, but the kernel must still allow the parent to retrieve its
        child's exit status, or learn that the child was terminated by the kernel.

        wait must fail and return -1 immediately if any of the following conditions is true:
            pid does not refer to a direct child of the calling process. pid is a direct child of the calling
            process if and only if the calling process received pid as a return value from a successful call to
            exec.

            Note that children are not inherited: if A spawns child B and B spawns child process C, then A cannot
            wait for C, even if B is dead. A call to wait(C) by process A must fail. Similarly, orphaned processes
            are not assigned to a new parent if their parent process exits before they do.

            The process that calls wait has already called wait on pid. That is, a process may wait for any
            given child at most once. 

        Processes may spawn any number of children, wait for them in any order, and may even exit without
        having waited for some or all of their children. Your design should consider all the ways in which waits can occur.
        All of a process's resources, including its struct thread, must be freed whether its parent ever waits for it or not,
        and regardless of whether the child exits before or after its parent.

        You must ensure that Pintos does not terminate until the initial process exits. The supplied Pintos code tries to do
        this by calling process_wait() (in "userprog/process.c") from main() (in "threads/init.c"). We suggest that you
        implement process_wait() according to the comment at the top of the function and then implement the wait system call in terms of process_wait().

        Implementing this system call requires considerably more work than any of the rest.

    */
   return -1;

}

int sys_write (int fd, char *buffer, unsigned size) {
    /*
    System Call: int write (int fd, const void *buffer, unsigned size)
        Writes size bytes from buffer to the open file fd. Returns the number of bytes actually
        written, which may be less than size if some bytes could not be written.

        Writing past end-of-file would normally extend the file, but file growth is not
        implemented by the basic file system. The expected behavior is to write as many
        bytes as possible up to end-of-file and return the actual number written, or 0
        if no bytes could be written at all.

        Fd 1 writes to the console. Your code to write to the console should write all of
        buffer in one call to putbuf(), at least as long as size is not bigger than a few
        hundred bytes. (It is reasonable to break up larger buffers.) Otherwise, lines of
        text output by different processes may end up interleaved on the console, confusing
        both human readers and our grading scripts.
    */

   struct file* write_file;
   int bytes_write;

   if(fd == 1){
       putbuf(buffer, size);
       return size;
   } else {
       //Make sure fd is valid
       write_file = thread_current()->pcb.file_descriptor_table[fd];
       if(write_file == NULL){
             return -1;
        }
       //Write to the fd
       aquire_fs_lock();
       bytes_write = file_write(write_file, buffer, size);
       release_fs_lock();
       //Return write size
   }
   return bytes_write;

}

int sys_open(const char *file){
    /*
    System Call: int open (const char *file)
        Opens the file called file. Returns a nonnegative integer handle called a "file descriptor" (fd),
        or -1 if the file could not be opened.

        File descriptors numbered 0 and 1 are reserved for the console: fd 0 (STDIN_FILENO) is standard
        input, fd 1 (STDOUT_FILENO) is standard output. The open system call will never return either of
        these file descriptors, which are valid as system call arguments only as explicitly described below.

        Each process has an independent set of file descriptors. File descriptors are not inherited by
        child processes.

        When a single file is opened more than once, whether by a single process or different processes,
        each open returns a new file descriptor. Different file descriptors for a single file are closed independently in separate calls to close and they do not share a file position.
    */
    struct file* file_opened;
    int file_descriptor_opened;

    if(!valid_pointer((void *) file)){
        sys_exit(-1);
    }

    //Open the file
    aquire_fs_lock();
    file_opened = filesys_open(file);
    release_fs_lock();
    if(file_opened == NULL){
        return -1;
    }
    //Place file into PCB
    file_descriptor_opened = thread_current()->pcb.number_open_files + 2;
    thread_current()->pcb.file_descriptor_table[file_descriptor_opened] = file_opened;
    thread_current()->pcb.number_open_files++;

    //printf("sys_open returning fd %i\n", file_descriptor_opened);
    return(file_descriptor_opened);
}

bool sys_create(const char *file, unsigned initial_size){
    /*
    System Call: bool create (const char *file, unsigned initial_size)
        Creates a new file called file initially initial_size bytes in size. Returns true if successful, false 
        otherwise. Creating a new file does not open it: opening the new file is a separate operation which would
        require a open system call. 
    */
    bool file_created = false;
    if(!valid_pointer((void *) file)){
        sys_exit(-1);
    }
    aquire_fs_lock();
    file_created = filesys_create(file, initial_size);
    release_fs_lock();
    return(file_created);
}

bool sys_remove(const char *file){
    /*
    System Call: bool remove (const char *file)
        Deletes the file called file. Returns true if successful, false otherwise. A file may be removed regardless of
        whether it is open or closed, and removing an open file does not close it. See Removing an Open File, for details. 
    */
    bool file_removed = false;
    if(!valid_pointer((void *) file)){
        sys_exit(-1);
    }
    aquire_fs_lock();
    file_removed = filesys_remove(file);
    release_fs_lock();
    return(file_removed);
}

int sys_read(int fd, void *buffer, unsigned size){
    /*
    System Call: int read (int fd, void *buffer, unsigned size)

        Reads size bytes from the file open as fd into buffer. Returns the number of bytes actually read (0 at end of file), or -1
        if the file could not be read (due to a condition other than end of file). Fd 0 reads from the keyboard using input_getc().
    */
    struct file* read_file;
    int bytes_read = 0;
    //printf("sys_read %i\n", fd);
    //Reading from a file from sys_open()
    if(fd > 0 && fd < MAX_NUMBER_OF_FILES_IN_PROCESS){
        read_file = thread_current()->pcb.file_descriptor_table[fd];
        if(read_file == NULL){
             return -1;
        }
        //("sys_read %i\n", bytes_read);
        aquire_fs_lock();
        bytes_read = file_read(read_file, buffer, size);
        release_fs_lock();
        //printf("sys_read %i\n", bytes_read);
    } else {
        return -1;
    }

    return bytes_read;
}

int sys_filesize(int fd){
    /*
    System Call: int filesize (int fd)

    Returns the size, in bytes, of the file open as fd.
    */
    uint32_t f_length = 0;

    struct file* read_file;
    read_file = thread_current()->pcb.file_descriptor_table[fd];
    if(read_file == NULL){
            return -1;
    }
    aquire_fs_lock();
    f_length = file_length(read_file);
    release_fs_lock();
    return(f_length);
}

void aquire_fs_lock(void){
    lock_acquire(&file_lock);
}
void release_fs_lock(void){
    lock_release(&file_lock);
}

int valid_pointer(void* provided_pointer){
    if(provided_pointer == NULL){
        return 0;
    }
    //if(provided_pointer > PHYS_BASE || provided_pointer < 0x08084000){
    if(provided_pointer > PHYS_BASE){
        return 0;
    }
    if(get_user((uint8_t*) provided_pointer) == -1){
        return 0;
    }
    return 1;
}

int valid_arg(void* arg_address){
    if(arg_address >= 0xC0000000){
        return 0;
    }
    return 1;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}