/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
bool load_file(struct page *page, void *aux);
struct mmap_file *find_mmfile(void *addr);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

    struct thread *curr = thread_current();
    struct file *refile = file_reopen(file);
    struct file_page *fp;
    struct page *mpage;

    struct mmap_file *mmap_file = (struct mmap_file *)malloc(sizeof(struct mmap_file));
	if (!mmap_file) {
		// Handle error...
		return NULL;
	}
    if (refile == NULL || mmap_file == NULL)
    {
        return NULL;
    }

    list_init(&mmap_file->page_list);
    list_push_back(&curr->mmap_list, &mmap_file->m_elem);

    mmap_file->start = addr;

    while (length > 0)
    {
        size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        fp = (struct file_page *)malloc(sizeof(struct file_page));
		if (!fp) {
			// Handle error...
			return NULL;
		}
        fp->file = refile;
        fp->ofs = offset;
        fp->read_bytes = page_read_bytes;
        fp->zero_bytes = page_zero_bytes;

        if (!vm_alloc_page_with_initializer(VM_FILE, addr,
                                            writable, load_file, fp))
        {
            file_close(refile);
            return NULL;
        }

        mpage = spt_find_page(&curr->spt, addr);
        list_push_back(&mmap_file->page_list, &mpage->mp_elem);

        length -= page_read_bytes;
        offset += page_read_bytes;
        addr += PGSIZE;
    }

    return mmap_file->start; // 시작 주소 리턴

}

/* Do the munmap */
void
do_munmap (void *addr) {

	struct supplemental_page_table *spt = &thread_current()->spt;
    struct page *page = spt_find_page(spt, addr);
    if (page->operations->type == VM_UNINIT)
    {
        vm_claim_page(addr);
    }
    struct file *file = page->file.file;
    struct mmap_file *mmap_file = find_mmfile(addr);
    struct list *pagelist = &mmap_file->page_list;

    struct list_elem *p;
    while (!list_empty(pagelist))
    {
        page = list_entry(list_pop_front(pagelist), struct page, mp_elem);
        if (pml4_is_dirty(thread_current()->pml4, page->va))
        {
            file_write_at(file, page->frame->kva, page->file.read_bytes, page->file.ofs);
        }
        hash_delete(&spt->page_info, &page->h_elem);
        spt_remove_page(spt, page);
    }

    // 스레드에 mf 를 위한 락을 하나 설정해주자 나중에
    file_close(file);
    list_remove(&mmap_file->m_elem);
    free(mmap_file);

}


bool load_file(struct page *page, void *aux)
{
    ASSERT(page->frame != NULL);
    ASSERT(aux != NULL);

    struct file_page *fp = (struct file_page *)aux;
    struct file *file = fp->file;
    off_t offset = fp->ofs;
    size_t page_read_bytes = fp->read_bytes;
    size_t page_zero_bytes = fp->zero_bytes;

    free(aux);

    page->file = (struct file_page){
        .file = file,
        .ofs = offset,
        .read_bytes = page_read_bytes,
        .zero_bytes = page_zero_bytes
    };

    void *kpage = page->frame->kva;

    if (file_read_at(file, kpage, page_read_bytes, offset) != (int)page_read_bytes)
    {
        vm_dealloc_page(page);
        return false;
    }

    memset(kpage + page_read_bytes, 0, page_zero_bytes);

    return true;
}

struct mmap_file *find_mmfile(void *addr)
{
    struct thread *curr = thread_current();
    struct list *mlist = &curr->mmap_list;
    struct list_elem *p;
    for (p = list_begin(mlist); p != list_end(mlist); p = list_next(p))
    {
        struct mmap_file *mmap_file = list_entry(p, struct mmap_file, m_elem);
        if (mmap_file->start == addr)
            return mmap_file;
    }
    return NULL;
}