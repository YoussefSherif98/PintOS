#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <hash.h>

//lock used for the open file during system calls so no other processes can change it
extern struct lock files_sync_lock;

//struct contains the file descriptor and pointer to the desired file to open
struct open_file {
    int fd;
    struct list_elem elem;
    struct file *file;
};
void exit(int status);
void syscall_init (void);

#endif /* userprog/syscall.h */
