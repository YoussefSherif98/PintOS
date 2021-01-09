#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

//struct for the child process contains its tid, a pointer to the thread itself and its status
struct child_process {
    tid_t tid;
    struct thread *t;
    int status;
    struct list_elem elem;
};
//used as a container for the arguments and a semaphore to synchronize untill child is loaded
struct arguments {
  int argc; //number of arguments in the list
  char **argv; //list of arguments
  bool status; //loaded of not
  struct semaphore load_child; //for synchronization
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
struct open_file * get_open_file (int fd); //get the desired file from the list of open files
void remove_open_file (int fd); //remove the file comepletely from the list and free from memory

#endif /* userprog/process.h */
