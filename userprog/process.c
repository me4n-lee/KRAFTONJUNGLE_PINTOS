#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "userprog/syscall.h"
// #define VM
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
void argument_stack(char **argv, int argc, struct intr_frame *if_);
struct thread *get_child_process (int pid);

void argument_stack(char **argv, int argc, struct intr_frame *if_) { // if_는 인터럽트 스택 프레임 => 여기에다가 쌓는다.

	/* insert arguments' address */
	char *arg_address[128];

	// 거꾸로 삽입 => 스택은 반대 방향으로 확장하기 떄문!
	
	/* 맨 끝 NULL 값(arg[4]) 제외하고 스택에 저장(arg[0] ~ arg[3]) */
	for (int i = argc-1; i>=0; i--) { 
		int argv_len = strlen(argv[i]);
		/* 
		if_->rsp: 현재 user stack에서 현재 위치를 가리키는 스택 포인터.
		각 인자에서 인자 크기(argv_len)를 읽고 (이때 각 인자에 sentinel이 포함되어 있으니 +1 - strlen에서는 sentinel 빼고 읽음)
		그 크기만큼 rsp를 내려준다. 그 다음 빈 공간만큼 memcpy를 해준다.
		 */
		if_->rsp = if_->rsp - (argv_len + 1);
		memcpy(if_->rsp, argv[i], argv_len+1);
		arg_address[i] = if_->rsp; // arg_address 배열에 현재 문자열 시작 주소 위치를 저장한다.
	}

	/* word-align: 8의 배수 맞추기 위해 padding 삽입*/
	while (if_->rsp % 8 != 0) 
	{
		if_->rsp--; // 주소값을 1 내리고
		*(uint8_t *) if_->rsp = 0; //데이터에 0 삽입 => 8바이트 저장
	}

	/* 이제는 주소값 자체를 삽입! 이때 센티넬 포함해서 넣기*/
	
	for (int i = argc; i >=0; i--) 
	{ // 여기서는 NULL 값 포인터도 같이 넣는다.
		if_->rsp = if_->rsp - 8; // 8바이트만큼 내리고
		if (i == argc) { // 가장 위에는 NULL이 아닌 0을 넣어야지
			memset(if_->rsp, 0, sizeof(char **));
		} else { // 나머지에는 arg_address 안에 들어있는 값 가져오기
			memcpy(if_->rsp, &arg_address[i], sizeof(char **)); // char 포인터 크기: 8바이트
		}	
	}
	

	/* fake return address */
	if_->rsp = if_->rsp - 8; // void 포인터도 8바이트 크기
	memset(if_->rsp, 0, sizeof(void *));

	if_->R.rdi  = argc;
	if_->R.rsi = if_->rsp + 8; // fake_address 바로 위: arg_address 맨 앞 가리키는 주소값!
}

struct thread *get_child_process (int pid) {
	struct thread *cur = thread_current();
	struct list *child_list = &cur->children_list;
	struct list_elem *cur_child = list_begin (child_list);

	while (cur_child != list_end (child_list)) {
		struct thread *cur_t = list_entry (cur_child, struct thread, child_elem);
		if (cur_t->tid == pid) {
			return cur_t;
		}
		cur_child = list_next (cur_child);
	}
	return NULL;
}

/* initd 및 기타 프로세스에 대한 일반 프로세스 초기화. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* "initd"라는 첫 번째 사용자 영역 프로그램을 시작합니다. FILE_NAME에서 로드됩니다.
새 스레드는 process_create_initd()가 반환되기 전에 스케줄링되거나 (심지어 종료될 수도 있습니다).
initd의 스레드 id를 반환하거나, 스레드를 생성할 수 없는 경우 TID_ERROR를 반환합니다.
이 함수는 한 번만 호출되어야 합니다. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);
	char *save_ptr;
	strtok_r (file_name, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	// printf("table pass\n");
	process_init ();

	// printf("p_init pass\n");
	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	// return thread_create (name,
	// 		PRI_DEFAULT, __do_fork, thread_current ());

	struct thread *cur = thread_current ();
	tid_t ctid = thread_create (name, PRI_DEFAULT, __do_fork, cur);

	if (ctid == TID_ERROR)
		return TID_ERROR;

	struct thread *child = get_child_process (ctid);
	sema_down (&cur->sema_fork);
	return ctid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	if (is_kernel_vaddr (va))
		return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	if (parent_page == NULL)
		return false;

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER);

	if (newpage == NULL)
		return false;  

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy (newpage, parent_page, PGSIZE);
	writable = is_writable (pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;
	parent_if = &parent->ptf;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	int cnt = 2;
	struct file **table = parent->fdt;

	while (cnt < 128) {
		if (table[cnt]) {
			current->fdt[cnt] = file_duplicate (table[cnt]);
		} else {
			current->fdt[cnt] = NULL;
		}
		cnt++;
	}
	current->next_fd = parent->next_fd;
 	sema_up (&parent->sema_fork);

	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	// thread_exit ();
	// sema_up (&parent->sema_fork);
	exit (TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	// printf("%s\n", f_name);

	
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();
	// printf("cleanup_failed\n");
	/* And then load the binary */
	// success = load (file_name, &_if);

	/*-----------------------------------------------------------------------------*/
	
	// //memset(&_if, 0, sizeof _if);
	// char file_name_copy[128]; // 스택에 저장
	// // file_name_copy = palloc_get_page(PAL_USER); // 이렇게는 가능 but 비효율적.
	// // strlen에 +1? => 원래 문자열에는 \n이 들어가는데 strlen에서는
	// // \n 앞까지만 읽고 끝내기 때문. 전체를 들고오기 위해 +1
	// memcpy(file_name_copy, file_name, strlen(file_name)+1);


	lock_acquire(&filesys_lock);
    success = load(file_name, &_if);
	// printf("%s\n", success ? "true" : "false");
    lock_release(&filesys_lock);
	/*-----------------------------------------------------------------------------*/


	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	/*-----------------------------------------------------------------------------*/


	// while (1){
	// }

	struct thread *child = get_child_process(child_tid);

	if (child == NULL)
		return -1;

	sema_down (&child->sema_wait);
	int exit_status = child->exit_status;
	list_remove (&child->child_elem);
	sema_up (&child->sema_exit);
	return exit_status;


	/*-----------------------------------------------------------------------------*/


	return -1;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	struct file **table = curr->fdt;
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	// if (curr->running_file)
	// 	file_close (curr->running_file);

	// int cnt = 2;
	// while (cnt < 128) {
	// 	if (table[cnt]) {
	// 		file_close (table[cnt]);
	// 		table[cnt] = NULL;
	// 	}
	// 	cnt++;
	// }
	// sema_up(&curr->sema_wait);
	// sema_down(&curr->sema_exit);
	// palloc_free_page(table);

	// process_cleanup ();


	int cnt = 2;
	while (cnt < 128) {
		if (table[cnt]) {
			file_close (table[cnt]);
			table[cnt] = NULL;
		}
		cnt++;
	}
	palloc_free_page(table);

	if (curr->running_file)
		file_close (curr->running_file);

	process_cleanup ();

	sema_up(&curr->sema_wait);

	sema_down(&curr->sema_exit);

}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
	// printf("failed\n");
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		// printf("pml4_create failed\n");
		goto done;
	process_activate (thread_current ());

	/*-----------------------------------------------------------------------------*/

	char *argv[128];
	char *token, *save_ptr;
	int argc = 0;

	

	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)) {
        argv[argc++] = token;

		// printf("%s\n", token);
    }

	file_name = argv[0];

	/*-----------------------------------------------------------------------------*/

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	t->running_file = file;
	file_deny_write (file);

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable)){
						
						// printf("load_segment failed\n");
						goto done;

					}
					
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_)){

		// printf("setup_stack failed");
		goto done;

	}

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	/*-----------------------------------------------------------------------------*/

	argument_stack(argv, argc, if_);
	// printf("argument_stack failed\n");

	// if_->R.rdi = argc;
	// if_->R.rsi = if_->rsp;
	success = true;

	/*-----------------------------------------------------------------------------*/


done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * 당신이 오직 project 2에 대한 함수만 구현하려고 한다면, 위 블록에서 이를 수행하세요. */

bool lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	/* TODO: 파일로부터 세그먼트를 로드하세요 */
	/* TODO: 이 함수는 주소 VA에서 첫 페이지 폴트가 발생할 때 호출됩니다. */
	/* TODO: 이 함수를 호출할 때 VA는 사용 가능합니다. */

	/*------------- project 3 -------------*/

	struct file_page *load_info = aux;
	void *kernel_page = page->frame->kva;


	file_seek(load_info->file, load_info->ofs);

	if (file_read(load_info->file, kernel_page, load_info->read_bytes) != (int)(load_info->read_bytes)){

		palloc_free_page(kernel_page);
		return false;

	}
	
	memset(kernel_page + load_info->read_bytes, 0, load_info->zero_bytes);

	// free(page_info);

	return true;

	/*------------- project 3 -------------*/

}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
/*주어진 파일의 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 로드합니다. 
총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 다음과 같이 초기화됩니다:

- UPAGE에서 시작하는 READ_BYTES 바이트는 파일의 OFS 오프셋에서 읽어야 합니다.
- UPAGE + READ_BYTES에서 시작하는 ZERO_BYTES 바이트는 0으로 채워져야 합니다.
이 함수에 의해 초기화된 페이지는 WRITABLE이 true일 경우 사용자 프로세스에 의해 쓰기 가능해야 하며,
그렇지 않으면 읽기 전용이어야 합니다.

메모리 할당 오류나 디스크 읽기 오류가 발생할 경우 false를 반환합니다. 성공적인 경우 true를 반환합니다.*/

static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);
	// printf("11111111111\n");
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		/* 이 페이지를 어떻게 채울지 계산합니다.
		 * 우리는 파일에서 PAGE_READ_BYTES 바이트를 읽을 것이며,
		 * 마지막 PAGE_ZERO_BYTES 바이트는 0으로 채울 것입니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/*------------- project 3 -------------*/

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		/* TODO: lazy_load_segment에 정보를 전달하기 위해 aux를 설정하세요. */
		// void *aux = NULL;
		// if (!vm_alloc_page_with_initializer (VM_ANON, upage,
		// 			writable, lazy_load_segment, aux))
		// 	return false;

		struct file_page *load_info = (struct file_page *)malloc(sizeof(struct file_page));
		if(load_info == NULL){
			// printf("load_info failed\n");
			return false;

		}

		load_info->file = file;
		load_info->ofs = ofs;
		load_info->read_bytes = page_read_bytes;
		load_info->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer (VM_ANON, upage, writable, lazy_load_segment, load_info)){
			// printf("vm_alloc_page_with_initializer failed\n");
			// free(load_info);	
			return false;
			
		}
	
		/* Advance. */
		/* 다음 단계로 진행합니다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;

		/*------------- project 3 -------------*/

	}
	return true;
}

/* USER_STACK에서 페이지 하나를 스택으로 생성합니다. 성공 시 true를 반환합니다. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	/* TODO: 스택을 stack_bottom에 매핑하고 페이지를 즉시 요청하세요.
	 * TODO: 성공 시, rsp를 그에 맞게 설정하세요.
	 * TODO: 페이지가 스택임을 표시해야 합니다. */
	/* TODO: 여기에 코드를 작성하세요 */

	/*------------- project 3 -------------*/
	
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1))
	// VM_MARKER_0: 스택이 저장된 메모리 페이지임을 식별하기 위해 추가
	// writable: argument_stack()에서 값을 넣어야 하니 True
	{
		// printf("load");
		// 2) 할당 받은 페이지에 바로 물리 프레임을 매핑한다.
		success = vm_claim_page(stack_bottom);
		if (success)
			// 3) rsp를 변경한다. (argument_stack에서 이 위치부터 인자를 push한다.)
			if_->rsp = USER_STACK;
	}

	/*------------- project 3 -------------*/

	return success;
}
#endif /* VM */