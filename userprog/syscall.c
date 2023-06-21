#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "vm/vm.h"

#include "vm/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {

	lock_init (&filesys_lock);

	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	// thread_exit ();
	#ifdef VM
	
	struct thread *curr = thread_current();
	curr->rsp = f->rsp;

	#endif

	switch (f->R.rax) {
		case SYS_HALT:
			halt ();
			break;
		case SYS_EXIT:
			exit (f->R.rdi);
			break;
		case SYS_FORK:
			memcpy (&thread_current ()->ptf, f, sizeof (struct intr_frame));
			f->R.rax = fork (f->R.rdi);
			break;
		case SYS_EXEC:
			exec (f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait (f->R.rdi);
			break; 
		case SYS_CREATE:
			f->R.rax = create (f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove (f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open (f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize (f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read (f->R.rdi, f->R.rsi, f->R.rdx);
			break;  
		case SYS_WRITE:      
			f->R.rax = write (f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek (f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell (f->R.rdi);
			break;
		case SYS_CLOSE:
			close (f->R.rdi);
			break;

		#ifdef VM
		case SYS_MMAP:
        	f->R.rax = (uint64_t)mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
        	break;
    	case SYS_MUNMAP:
        	munmap(f->R.rdi);
		#endif

		default:
			exit (-1);
			break;
	}
}

void halt (void) {
	power_off ();
}

void exit (int status) {
	struct thread *cur = thread_current ();

	cur->exit_status = status;
	printf ("%s: exit(%d)\n", cur->name, status);
	thread_exit ();
}

int fork (const char *thread_name) {
	check_address (thread_name);
	return process_fork (thread_name, &thread_current ()->ptf);
}

int exec (const char *file_name) {
	check_address (file_name);

	int file_size = strlen (file_name) + 1;
	char *fn_copy = palloc_get_page (PAL_ZERO);

	if (!fn_copy) {
		exit (-1);
		return -1;
	}

	strlcpy (fn_copy, file_name, file_size);

	if (process_exec (fn_copy) == -1) {
		exit (-1);
		return -1;
	}
}

int wait (tid_t pid) {
  	return process_wait (pid);
}

bool create (const char *file, unsigned initial_size) {
	check_address (file);
	return filesys_create (file, initial_size);
}

bool remove (const char *file) {
	check_address (file);
	return filesys_remove (file);
}

int open (const char *file) {
	check_address (file);

	struct thread *cur = thread_current ();
	struct file *fd = filesys_open (file);

	if (fd) {
		for (int i = 2; i < 128; i++) {
			if (!cur->fdt[i]) {
				cur->fdt[i] = fd;
				cur->next_fd = i + 1;
				return i;
			}
		}
		file_close(fd);
	}

	return -1;
}

int filesize (int fd) {
	struct file *file = thread_current ()->fdt[fd];

	if (file)
		return file_length (file);
	return -1;
}

int read (int fd, void *buffer, unsigned size) {
	check_address (buffer);

	if (fd == 1) {
		
		return -1;
	}

	if (fd == 0) {
		
		lock_acquire (&filesys_lock);
		int byte = input_getc ();
		lock_release (&filesys_lock);
		
		return byte;
		
	}
	struct file *file = thread_current ()->fdt[fd];
	#ifdef VM
    struct page *read_page = spt_find_page(&thread_current()->spt, buffer);
      	if(read_page && !read_page->writable){
        	 exit(-1);
    }
    #endif

	if (file) {
		lock_acquire (&filesys_lock);
		int read_byte = file_read (file, buffer, size);
		lock_release (&filesys_lock);
		
		
		return read_byte;
		
	}

	return -1;
}

int write (int fd UNUSED, const void *buffer, unsigned size) {
	check_address (buffer);

	if (fd == 0)
		return -1;

	if (fd == 1) {
		lock_acquire (&filesys_lock);
		putbuf (buffer, size);
		lock_release (&filesys_lock);
		return size;
	}

	struct file *file = thread_current ()->fdt[fd];

	if (file) {
		lock_acquire (&filesys_lock);
		int write_byte = file_write (file, buffer, size);
		lock_release (&filesys_lock);
		return write_byte;
	}
}

void seek (int fd, unsigned position) {
	struct file *curfile = thread_current ()->fdt[fd];

	if (curfile)
		file_seek (curfile, position);
}

unsigned tell (int fd) {
	struct file *curfile = thread_current ()->fdt[fd];

	if (curfile)
		return file_tell (curfile);
}

void close (int fd) {
	struct file * file = thread_current ()->fdt[fd];

	if (file) {
		lock_acquire (&filesys_lock);
		thread_current ()->fdt[fd] = NULL;
		file_close (file);
		lock_release (&filesys_lock);
	}
}

void check_address (void *addr) {
	if (addr == NULL || !is_user_vaddr((uint64_t)addr))
    {
        exit(-1);
    }
}

#ifdef VM

static void fd_validate(int fd)
{
    if (fd < 0 || fd >= 128)
    {
        exit(-1);
    }
}


#define FDT_MAX_COUNT 128

static struct file *
get_file(int fd)
{
    if (fd < 2 || fd >= FDT_MAX_COUNT)
        exit(-1);
    struct file *file = *(thread_current()->fdt + fd);
    if (file == NULL)
        exit(-1);
    return file;
}

static bool mmap_validate(void *addr, size_t length, off_t offset)
{
    if (addr == NULL || ((uint64_t)addr % PGSIZE))
        return false;

    if (spt_find_page(&thread_current()->spt, addr))
    {
        return false;
    }

    if (!is_user_vaddr((uint64_t)addr) || !is_user_vaddr(addr + length))
        return false;

    if ((int)length <= 0)
        return false;

    if (offset > length)
        return NULL;

    return true;
}

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{
    fd_validate(fd);
    if (!mmap_validate(addr, length, offset))
    {
        return NULL;
    }
    // validate_address(addr);

    struct thread *curr = thread_current();
    struct file *file = get_file(fd);

    if (file == NULL)
        return NULL;

    off_t filelength = file_length(file);

    if (length > filelength)
        length = filelength;

    if (offset > length)
        return NULL;

    return do_mmap(addr, length, writable, file, offset);
}

void munmap(void *addr)
{
    check_address(addr);
    // addr 가 PGSIZE 배수가 아니면 또 에러
    if (((uint64_t)addr % PGSIZE) != 0)
        exit(-1);

    struct mmap_file *mf = find_mmfile(addr);
    if (mf == NULL)
        exit(-1);

    do_munmap(addr);
}

#endif