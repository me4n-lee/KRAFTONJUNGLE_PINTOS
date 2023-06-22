/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"
// #include "lib/kernel/hash.h"

static struct list frame_list;
static struct lock frame_lock;

#define USER_STACK_LIMIT (1 << 20)

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
	list_init(&frame_list);
	lock_init(&frame_lock);

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

/* 초기화 프로그램과 함께 보류 중인 페이지 객체를 생성합니다. 페이지를 생성하려면 이 함수 또는
vm_alloc_page를 통해 직접 만들지 않고 생성해야 합니다. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/*------------- project 3 -------------*/
	
	struct page *p;

	/*  upage가 이미 사용 중인지 아닌지 확인합니다.  */
	if (spt_find_page (spt, upage) == NULL) {

		// TODO: 페이지를 생성하고, VM 타입에 따라 초기화 프로그램을 가져옵니다.
		// TODO: 그리고 uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다.
		// TODO: uninit_new 호출 후 필드를 수정해야 합니다.

		/* TODO: 페이지를 spt에 삽입합니다. */

		p = (struct page*) malloc(sizeof(struct page));

		bool (*page_initializer)(struct page *, enum vm_type, void *);

		switch (VM_TYPE(type)){
			case VM_ANON:
				page_initializer = anon_initializer;
				break;
			case VM_UNINIT:
			case VM_FILE:
				page_initializer = file_backed_initializer;
				break;

		}

		uninit_new(p, upage, init, type, aux, page_initializer);
		
		p->writable = writable;

		// if (spt_insert_page(spt, p)){
		// 	// printf("insert success\n");
  		// 	return true;
		// } 
		// else {
		// 	// printf("insert c\n");
  		// 	goto err;
		// }

		return spt_insert_page(spt, p);
	}
	/*------------- project 3 -------------*/

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

	p.va = pg_round_down(va);												// 인스턴스인 p 생성, 그 va 필드에서 찾고자 하는 가상 주소를 저장한다.
	e = hash_find(&spt->page_info, &p.h_elem);				// 주어진 spt의 해시 테이블에서 해당 가상 주소(va)를 키로 가지는 해시 요소를 찾는다.

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
	hash_delete(&spt->page_info, &page->h_elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	struct list_elem *e;
	lock_acquire(&frame_lock);

	for(e = list_begin(&frame_list); e != list_end(&frame_list); e = list_next(e)){

		victim = list_entry(e,struct frame, f_elem);
		uint64_t *pml4 = thread_current()->pml4;

		if(victim->page == NULL){
			break;
		}

		if(pml4_is_accessed(pml4,victim->page->va)){
			pml4_set_accessed(pml4,victim->page->va,0);
		}else{
			break;
		}
	}

	lock_release(&frame_lock);

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if (victim->page != NULL){
		// 어디로 가는거냐고
        swap_out(victim->page);
	}

	return victim;
}

/* palloc()을 사용하여 프레임을 가져옵니다. 
사용 가능한 페이지가 없으면 페이지를 추방(evict)하고 반환합니다.
이 함수는 항상 유효한 주소를 반환합니다. 즉, 사용자 풀 메모리가 가득 찬 경우, 
이 함수는 프레임을 추방하여 사용 가능한 메모리 공간을 얻습니다. */
static struct frame * vm_get_frame (void) {
	
	// struct frame *frame = NULL;
	// /* TODO: Fill this function. */

	// /*------------- project 3 -------------*/

	// /* palloc_get_page를 호출하여 사용자 풀로부터 새로운 물리 페이지를 가져옵니다. */
	// void *kva = palloc_get_page(PAL_USER);

	// if(!kva){

	// 	PANIC("todo");		

	// }

	// frame = (struct frame *)malloc(sizeof(struct frame));

	// frame->kva = kva;
	// frame->page = NULL;

	// /*------------- project 3 -------------*/

	// ASSERT (frame != NULL);
	// ASSERT (frame->page == NULL);
	// return frame;

	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void *kva = palloc_get_page(PAL_USER | PAL_ZERO);
	
	if(kva == NULL){
		/* while(frame == NULL){// 항상 유효한 주소를 반환해야함
			frame = vm_evict_frame();
		} */
		frame = vm_evict_frame();
		memset(frame->kva, 0, PGSIZE);
		frame->page = NULL;
		return frame;
	}
	frame = (struct frame *)malloc(sizeof(struct frame));
	frame->kva = kva;
	frame->page = NULL;

	lock_acquire(&frame_lock);
	list_push_back(&frame_list,&frame->f_elem);
	lock_release(&frame_lock);

	ASSERT(frame != NULL);
    ASSERT(frame->kva != NULL);
    ASSERT(frame->page == NULL);

	return frame;
	 
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {

	void *upage = pg_round_down(addr);
	vm_alloc_page(VM_ANON | VM_MARKER_0, upage, true);

}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;

	if (addr == NULL)
		return false;

	if (is_kernel_vaddr(addr))
		return false;

	if (not_present) // 접근한 메모리의 physical page가 존재하지 않은 경우
	{
		/* TODO: Validate the fault */
		// todo: 페이지 폴트가 스택 확장에 대한 유효한 경우인지를 확인해야 합니다.
		void *rsp = f->rsp; // user access인 경우 rsp는 유저 stack을 가리킨다.
		if (!user)			// kernel access인 경우 thread에서 rsp를 가져와야 한다.
			rsp = thread_current()->rsp;

		// 스택 확장으로 처리할 수 있는 폴트인 경우, vm_stack_growth를 호출
		if ((USER_STACK - (1 << 20) <= rsp - 8 &&
				rsp - 8 == addr && 
				addr <= USER_STACK) || (USER_STACK - (1 << 20) <= rsp && 
				rsp <= addr && 
				addr <= USER_STACK)){

			vm_stack_growth(addr);

		}
			

		page = spt_find_page(spt, pg_round_down(addr));
		if (page == NULL)
			return false;
		if (write && !page->writable) // write 불가능한 페이지에 write 요청한 경우
			return false;
		return vm_do_claim_page(page);
	}
	return false;
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
	// struct page *page = NULL;

	/*------------- project 3 -------------*/
	/* TODO: Fill this function */
	// va를 할당하기 위해 페이지를 주장합니다. 
	// 먼저 페이지를 가져온 다음 페이지를 가지고 vm_do_claim_page를 호출해야 합니다.
	// printf("load\n");
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page	*page = spt_find_page(spt, va);

	if (page == NULL){

		return false;

	}

	/*------------- project 3 -------------*/


	return vm_do_claim_page (page);
}

/* PAGE를 요구하고 mmu를 설정합니다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();


	/* 링크 설정 */
	frame->page = page;
	page->frame = frame;

	/* TODO: 페이지 테이블 항목을 삽입하여 페이지의 VA를 프레임의 PA로 매핑합니다. */

	/*------------- project 3 -------------*/
	// 페이지를 주장하며, 이는 물리적 프레임을 할당한다는 의미입니다. 
	// 먼저 vm_get_frame을 호출하여 프레임을 가져와야 합니다(이것은 템플릿에서 이미 완료되었습니다). 
	// 그 다음 MMU를 설정해야 합니다. 
	// 다시 말해, 페이지 테이블에서 가상 주소에서 물리 주소로의 매핑을 추가해야 합니다. 
	// 반환 값은 작업이 성공했는지 여부를 나타내야 합니다.

	/* 페이지 테이블 엔트리 삽입 */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
		
		return false;

	}

	/*------------- project 3 -------------*/

	return swap_in (page, frame->kva);
}


/* 새로운 보조 페이지 테이블 초기화 */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	
	/*------------- project 3 -------------*/
	// printf("TLWKR\n");
	hash_init(&spt->page_info, page_hash, page_less, NULL);
	// printf("dddd\n");
	/*------------- project 3 -------------*/
}

/* 보조 페이지 테이블을 src에서 dst로 복사 */
bool supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED) {
		
	// TODO: 보조 페이지 테이블을 src에서 dst로 복사합니다.
	// TODO: src의 각 페이지를 순회하고 dst에 해당 entry의 사본을 만듭니다.
	// TODO: uninit page를 할당하고 그것을 즉시 claim해야 합니다.

	struct hash_iterator iter;
    struct page *new_page;
    struct page *entry;
    enum vm_type type;

	// lock_release(&src->page_lock);

    hash_first(&iter, &src->page_info);
    while (hash_next(&iter))
    {
        entry = hash_entry(hash_cur(&iter), struct page, h_elem);
        type = entry->operations->type;

        // if (entry->frame == NULL)
        // {
        //     // printf("frame is null, so we have to swap in entry << \n");
        // }

        if (type == VM_UNINIT)
        {
            void *aux = entry->uninit.aux;
            enum vm_type real_type = page_get_type(entry);
            struct file_page *fp = NULL;
            if (aux != NULL)
            {
                fp = (struct file_page *)malloc(sizeof(struct file_page));
                struct file_page *tp = (struct file_page *)aux;
                fp->file = real_type == VM_FILE ? file_reopen(tp->file) : tp->file;
                fp->ofs = tp->ofs;
                fp->read_bytes = tp->read_bytes;
                fp->zero_bytes = tp->zero_bytes;
            }

            vm_alloc_page_with_initializer(real_type, entry->va, entry->writable, entry->uninit.init, fp);
        }
        else if (type == VM_ANON)
        {
            /*
                VM_ANON + MARKER_0 까지 포함해야 한다.
                참고로 offset 은 카피할 필요가 없다. 왜냐하면 offset 이 같으면 같은 스왑공간에 들어간다는 소리기 때문이죠.
                최초에 vm_claim_page 로 스왑인 된 후, 페이지를 카피하고 나면
                다시 스왑아웃 할 때. 본인만의 스왑 공간 위치(offset) 을 가지게 됨
            */
            if (!vm_alloc_page(type, entry->va, entry->writable))
            {
                printf("ANON 페이지 카피 실패\n");
                return false;
            }
            if (!vm_claim_page(entry->va))
                return false;

            void *dest = spt_find_page(dst, entry->va)->frame->kva;
            memcpy(dest, entry->frame->kva, PGSIZE);
        }
    }

    // lock_release(&src->page_lock);
    return true;
	
}

/* 보조 페이지 테이블이 유지하고 있는 리소스를 해제 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->page_info, hash_destructor);
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

void hash_destructor(struct hash_elem *e, void *aux){

	// struct page *p = hash_entry(e, struct page, h_elem);
	// destroy(p);
	// free(p);

	vm_dealloc_page(hash_entry(e,struct page, h_elem));

}

void vm_free_frame(struct frame *frame){
    
	lock_acquire(&frame_lock);
    list_remove(&frame->f_elem);
    lock_release(&frame_lock);
    free(frame);
}

/*--------------------------------------------------------------------*/
