#include <stdio.h>
#include <syscall-nr.h>

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"

static void syscall_handler(struct intr_frame *);

void
syscall_init(void)
{
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f UNUSED)
{
    uint32_t *usp = f->esp;

    uint32_t call_no = *usp;
    uint32_t arg0 = *(usp+1);
    uint32_t arg1 = *(usp+2);;
    uint32_t arg2 = *(usp+3);;
    uint32_t arg3 = *(usp+4);;

    switch(call_no) {
        case SYS_EXIT:
            sys_exit(arg0);
            break;
        case SYS_HALT:
            sys_halt();
            break;
        case SYS_WRITE :
            f->eax = sys_write((int) arg0, (char*) arg1, (unsigned) arg2);
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

   if(fd == 1){
       putbuf(buffer, size);
       return size;
   }

}

void sys_exit(int status){
    /*
    System Call: void exit (int status)
        Terminates the current user program, returning status to the kernel.
        If the process's parent waits for it (see below), this is the status that will be returned.
        Conventionally, a status of 0 indicates success and nonzero values indicate errors. 
    */

   struct thread *cur = thread_current();
   printf("%s: exit(%d)\n", cur->name, status);
   cur->exit_status = status;
   sema_up(&cur->exit_block);
   thread_exit();

}