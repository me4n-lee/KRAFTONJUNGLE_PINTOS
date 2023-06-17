/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
// #include "lib/kernel/hash.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* spt에서 VA를 찾아 페이지를 반환합니다. 오류가 발생하면 NULL을 반환합니다 */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// struct page *page = NULL;
	
	/* TODO: Fill this function. */

	/*------------- project 3 -------------*/
	// 주어진 보조 페이지 테이블에서 va에 해당하는 struct page를 찾습니다. 
	// 실패하면 NULL을 반환합니다.

	struct page p, *found_page;
	struct hash_elem *e;

	p.va = va;												// 인스턴스인 p 생성, 그 va 필드에서 찾고자 하는 가상 주소를 저장한다.
	e = hash_find(&spt->page_info, &p.h_elem);			// 주어진 spt의 해시 테이블에서 해당 가상 주소(va)를 키로 가지는 해시 요소를 찾는다.

	if(!e){													// 해시요소가 없다면, NULL 반환

		return NULL;
	
	}
	found_page = hash_entry(e, struct page, h_elem);		// 찾은 e를 페이지로 변환해 주는 과정
		 
	return found_page;										// found_page 반환

	/*------------- project 3 -------------*/

	//return page;
}

/* 유효성 검사와 함께 spt에 PAGE를 삽입합니다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {

	// int succ = false;
	/* TODO: Fill this function. */

	// return succ;

	/*------------- project 3 -------------*/
	// struct page를 주어진 보조 페이지 테이블에 삽입합니다.
	// 이 함수는 가상 주소가 주어진 보조 페이지 테이블에 존재하지 않는지 확인해야 합니다.

	struct hash_elem * e = hash_find(&spt->page_info, &page->h_elem);

	if (e != NULL){

		return false;

	}

	hash_insert(&spt->page_info, &page->h_elem);

	return true;

	/*------------- project 3 -------------*/

}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc()을 사용하여 프레임을 가져옵니다. 
사용 가능한 페이지가 없으면 페이지를 추방(evict)하고 반환합니다.
이 함수는 항상 유효한 주소를 반환합니다. 즉, 사용자 풀 메모리가 가득 찬 경우, 
이 함수는 프레임을 추방하여 사용 가능한 메모리 공간을 얻습니다. */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* VA에 할당된 페이지를 요구합니다. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */

	return vm_do_claim_page (page);
}

/* PAGE를 요구하고 mmu를 설정합니다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* 링크 설정 */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	return swap_in (page, frame->kva);
}


/* 새로운 보조 페이지 테이블 초기화 */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	
	/*------------- project 3 -------------*/

	hash_init(&spt->page_info, page_hash, page_less, NULL);

	/*------------- project 3 -------------*/
}

/* 보조 페이지 테이블을 src에서 dst로 복사 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* 보조 페이지 테이블이 유지하고 있는 리소스를 해제 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

/*--------------------------------------------------------------------*/

/* Returns a hash value for page p. */
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED) {

  	struct page *p = hash_entry(p_, struct page, h_elem);

  	return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
  
  	struct page *a = hash_entry(a_, struct page, h_elem);
  	struct page *b = hash_entry(b_, struct page, h_elem);

  	return a->va < b->va;
}

/*--------------------------------------------------------------------*/
