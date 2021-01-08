#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"

typedef int pid_t;

struct lock files_sync_lock; //lock used for the open file during system calls so no other processes can change it

static void syscall_handler(struct intr_frame *);

void 
syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&files_sync_lock);
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
  : "=&a" (result) : "m" (*uaddr));
  return result;
}

//validation
static void 
validate(void *ptr) {
  if (ptr != NULL && is_user_vaddr(ptr) && get_user(ptr) != -1)
    return;
  else
    exit(-1);
}

//get the arguments from the stack depending on the offset (1st, 2nd or 3rd)
static int 
get_argument(void *esp, int offset)
{
  int *arg = (int*) esp + offset;
  validate((char*)arg + 3);
  return *arg;
}

static void halt (void){
  shutdown_power_off();
  NOT_REACHED();
}

void exit (int status){
  printf("%s: exit(%d)\n", thread_current()->name, status);
  if(thread_current()->child != NULL)
  {
    thread_current()->child->status = status;
    thread_current()->child->t =NULL;
  }
  thread_exit();
  NOT_REACHED();
}

static pid_t exec (const char *cmd_line){
  return process_execute(cmd_line);
}

static int wait (pid_t pid){
  return process_wait(pid);
}

static bool create (const char *file, unsigned initial_size){
  lock_acquire(&files_sync_lock);
  bool status = filesys_create(file, initial_size);
  lock_release(&files_sync_lock);
  return status;
}

static bool remove (const char *file){
  bool success;
  lock_acquire(&files_sync_lock);
  success = filesys_remove(file);
  lock_release(&files_sync_lock);
  return success;
}

static int open (const char *file){
  lock_acquire(&files_sync_lock);
  struct file *f = filesys_open(file);
  lock_release(&files_sync_lock);
  if (f == NULL) {
    return -1;
  }
  struct open_file *of = malloc(sizeof(struct open_file));
  if (!of)
    return -1;
  of->file = f; //attach the open file
  of->fd = thread_current()->fd_last;
  thread_current()->fd_last++; //increment the file descriptor counter
  list_push_back(&thread_current()->open_files, &of->elem); //add to the list
  return of->fd;
}

static int filesize (int fd){
  struct open_file *of = get_open_file(fd);
  if (of == NULL || of == 0) {
    return -1;
  }
  lock_acquire(&files_sync_lock);
  int ans = file_length(of->file);
  lock_release(&files_sync_lock);
  return ans;
}

static int read (int fd, void *buffer, unsigned size){
  if (fd == 0) { //read from the buffer (keyboard)
    input_getc(buffer, size);
    return size;
  }
  else if (fd == 1) { //can't happen
    return 0;
  }
  struct open_file *of = get_open_file(fd); //get the open file
  if (of == NULL || of == 0) {
    return -1;
  }
  lock_acquire(&files_sync_lock);
  int ans = file_read(of->file, buffer, size); //read the file
  lock_release(&files_sync_lock);
  return ans;
}

static int write (int fd, const void *buffer, unsigned size){
  if (fd == 0) { //can't happen
    return 0;
  }
  else if (fd == 1) { //write to the buffer (monitor)
    putbuf(buffer, size);
    return size;
  }
  struct open_file *of = get_open_file(fd); //get the open file
  if (of == NULL || of == 0) {
    return -1;
  }
  lock_acquire(&files_sync_lock);
  int ans = file_write(of->file, buffer, size); //write the file
  lock_release(&files_sync_lock);
  return ans;
}

static void seek (int fd, unsigned position){
  struct open_file *of = get_open_file(fd);
  if (of == NULL || of == 0) {
    return -1;
  }
  lock_acquire(&files_sync_lock);
  file_seek(of->file, position);
  lock_release(&files_sync_lock);
}

static unsigned tell (int fd){
  struct open_file *of = get_open_file(fd);
  if (of == NULL || of == 0) {
    return -1;
  }
  lock_acquire(&files_sync_lock);
  file_tell(of->file);
  lock_release(&files_sync_lock);
}

static void close (int fd){
  lock_acquire(&files_sync_lock);
  remove_open_file(fd); //remove the open file from the list and free from memory
  lock_release(&files_sync_lock);
}

static void halt_wrapper (){
  halt();
}

void exit_wrapper (void* esp){
  int status = get_argument(esp, 1);
  exit(status);
}
static pid_t exec_wrapper (void* esp){
  const char *cmd_line = get_argument(esp, 1);
  validate((char*)cmd_line + 3);
  return exec(cmd_line);
}
static int wait_wrapper (void* esp){
  pid_t pid = get_argument(esp, 1);
  return wait(pid);
}
static bool create_wrapper (void* esp){
  const char *file = get_argument(esp, 1);
  unsigned initial_size = get_argument(esp, 2);
  validate((char*)file + 3);
  return create (file, initial_size);
}
static bool remove_wrapper (void* esp){
  const char *file = get_argument(esp, 1);
  validate((char*)file + 3);
  return remove(file);
}
static int open_wrapper (void* esp){
  const char *file = get_argument(esp, 1);
  validate((char*)file + 3);
  return open(file);
}
static int filesize_wrapper (void* esp){
  int fd = get_argument(esp, 1);
  return filesize(fd);
}
static int read_wrapper (void* esp){
  int fd = get_argument(esp, 1);
  void *buffer = get_argument(esp, 2);
  unsigned size = get_argument(esp, 3);
  validate((char*)buffer + size - 1);
  return read(fd, buffer, size);
}
static int write_wrapper(void* esp){
  int fd = get_argument(esp, 1);
  const void *buffer = get_argument(esp, 2);
  unsigned size = get_argument(esp, 3);
  validate((char*)buffer + size - 1);
  return write (fd, buffer, size);
}
static void seek_wrapper (void* esp){
  int fd = get_argument(esp, 1);
  unsigned position = get_argument(esp, 2);
  seek (fd, position);
}
static unsigned tell_wrapper (void* esp){
  int fd = get_argument(esp, 1);
  return tell (fd);
}
static void close_wrapper (void* esp){
  int fd = get_argument(esp, 1);
  close (fd);
}

static void
syscall_handler(struct intr_frame *f)
{
  void* esp = f->esp;
  switch ((int)get_argument(esp,0))
  {
  case SYS_HALT:
    halt_wrapper();
    break;
  case SYS_EXIT:
    exit_wrapper(esp);
    break;
  case SYS_EXEC:
    f->eax = exec_wrapper(esp);
    break;
  case SYS_WAIT:
    f->eax = wait_wrapper(esp);
    break;
  case SYS_CREATE:
    f->eax = create_wrapper(esp);
    break;
  case SYS_REMOVE:
    f->eax = remove_wrapper(esp);
    break;
  case SYS_OPEN:
    f->eax = open_wrapper(esp);
    break;
  case SYS_FILESIZE:
    f->eax = filesize_wrapper(esp);
    break;
  case SYS_READ:
    f->eax = read_wrapper(esp);
    break;
  case SYS_WRITE:
    f->eax = write_wrapper(esp);
    break;
  case SYS_SEEK:
    seek_wrapper(esp);
    break;
  case SYS_TELL:
    f->eax = tell_wrapper(esp);
    break;
  case SYS_CLOSE:
    close_wrapper(esp);
    break;
  default:
    break;
  }
}