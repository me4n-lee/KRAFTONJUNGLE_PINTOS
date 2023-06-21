#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "hash.h"

enum vm_type {
	/* page not initialized */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	VM_ANON = 1,
	/* page that realated to the file */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* "페이지"의 표현입니다.
이것은 네 가지 "자식 클래스", 즉
uninit_page, file_page, anon_page, 그리고 page cache (project4)를 가진 "부모 클래스"의 종류입니다.
이 구조체의 사전 정의된 멤버를 제거/수정하지 마십시오. */
struct page {
	const struct page_operations *operations;
	void *va;              /* 사용자 공간 측면의 주소 */
	struct frame *frame;   /* 프레임에 대한 역참조 */

	/* Your implementation */
	struct hash_elem h_elem;
	struct list_elem mp_elem;
	// 읽기, 쓰기 권한
	bool writable;
	int mapped_page_count;

	/* 타입별 데이터는 union으로 묶입니다.
	각 함수는 자동으로 현재 union을 감지합니다. */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
struct frame {
	void *kva;
	struct page *page;
	struct list_elem f_elem;
	struct thread *th;
	
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* 현재 프로세스의 메모리 공간 표현입니다.
이 구조체에 대해 어떠한 특정 디자인도 강제하고 싶지 않습니다.
이것에 대한 모든 디자인은 여러분에게 달려 있습니다. */
struct supplemental_page_table {

	// 보조 테이블에 있는 각 페이지 정보를 저장하고 관리하는 포인터
	struct hash page_info;
	
};

#include "threads/thread.h"

void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

unsigned page_hash (const struct hash_elem *p_, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);
void hash_destructor(struct hash_elem *e, void *aux);


struct lock lru_lock;

struct list frame_table;

#endif  /* VM_VM_H */
