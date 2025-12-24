#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include "mem.h"

void *ac_alloc_page(void) {
    struct page *page;
    page = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!page) {
        pr_err("failed to allocate page\n");
        return NULL;
    }
    SetPageReserved(page);

    return page_address(page);
}

void ac_free_page(void *page) {
    struct page *p = virt_to_page(page);
    ClearPageReserved(p);
    __free_page(p);
}

void *ac_alloc_pages(unsigned int order) {
    struct page *page;
    page = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
    for (int i = 0; i < (1 << order); i++) {
        SetPageReserved(page + i);
    }

    return page_address(page);
}

void ac_free_pages(void *page, unsigned int order) {
    struct page *p = virt_to_page(page);
    for (int i = 0; i < (1 << order); i++) {
        ClearPageReserved(p + i);
    }
    __free_pages(p, order);
}

static inline bool ac_mm_permcheck(u64 tent, enum ac_mm_access access, int cpl, int *fault) {
    u64 err = 0;

    if (cpl == 3)                 err |= PF_ERR_U;
    if (access & AC_MM_WRITE)     err |= PF_ERR_W;
    if (access & AC_MM_FETCH)     err |= PF_ERR_ID;

    if (!(tent & _PAGE_PRESENT))                     goto fault;
    if (cpl == 3 && !(tent & _PAGE_USER))            goto fault;
    if (access & AC_MM_WRITE && !(tent & _PAGE_RW))  goto fault;
    if (access & AC_MM_FETCH && (tent & _PAGE_NX))   goto fault;

    return true;

fault:
    if (fault != NULL) {
        *fault = err;
    }
    return false;
}

void *ac_mm_walk_pagetable(u64 cr3, u64 gva, enum ac_mm_access access, int cpl, int *fault) {
    u64 tab = cr3 & ~0xfffull, ent;

    if (get_kernel_nofault(ent, (((u64*)__va(tab)) + (gva >> 39 & 0x1ff))) < 0) goto fault; // L4 (512G)
    if (!ac_mm_permcheck(ent, access, cpl, fault)) goto fault;

    tab = PG_NEXT_ADDR(ent);
    if (get_kernel_nofault(ent, (((u64*)__va(tab)) + (gva >> 30 & 0x1ff))) < 0) goto fault; // L3 (1G)
    if (!ac_mm_permcheck(ent, access, cpl, fault)) goto fault;
    if (ent & _PAGE_PSE) {
        return __va((PG_NEXT_ADDR(ent) & ~0x3fffffffull) | (gva & 0x3fffffffull));
    }

    tab = PG_NEXT_ADDR(ent);
    if (get_kernel_nofault(ent, (((u64*)__va(tab)) + (gva >> 21 & 0x1ff))) < 0) goto fault; // L2 (2M)
    if (!ac_mm_permcheck(ent, access, cpl, fault)) goto fault;
    if (ent & _PAGE_PSE) {
        return __va((PG_NEXT_ADDR(ent) & ~0x1fffffull) | (gva & 0x1fffffull));
    }

    tab = PG_NEXT_ADDR(ent);
    if (get_kernel_nofault(ent, (((u64*)__va(tab)) + (gva >> 12 & 0x1ff))) < 0) goto fault; // L1 (4K)
    if (!ac_mm_permcheck(ent, access, cpl, fault)) goto fault;

    return __va((PG_NEXT_ADDR(ent) & ~0xfff) | (gva & 0xfff));

fault:
    pr_err("fault at %llx\n", gva);
    return 0;
}

int ac_mm_copy_from_guest(u8 *dst, size_t size, u64 cr3, u64 gva, enum ac_mm_access access, int cpl, struct ac_mm_fault *fault) {
    size_t idx = 0, can_read;
    int fault_code;
    void *src;

    while (size > 0) {
        can_read = min_t(size_t, size, PAGE_SIZE - (gva & ~PAGE_MASK));

        src = ac_mm_walk_pagetable(cr3, gva, access, cpl, &fault_code);
        if (!src) {
            if (fault) {
                fault->fault = fault_code;
                fault->gva = gva;
            }
            return -EFAULT;
        }

        // TODO: nofault vairant
        memcpy(dst + idx, src, can_read);

        idx += can_read;
        gva += can_read;
        size -= can_read;
    }

    return 0;
}

int ac_mm_copy_to_guest(u8 *src, size_t size, u64 cr3, u64 gva, enum ac_mm_access access, int cpl, struct ac_mm_fault *fault) {
    size_t idx = 0, can_write;
    int fault_code;
    void *dst;

    while (size > 0) {
        can_write = min_t(size_t, size, PAGE_SIZE - (gva & ~PAGE_MASK));

        dst = ac_mm_walk_pagetable(cr3, gva, access, cpl, &fault_code);
        if (!dst) {
            if (fault) {
                fault->fault = fault_code;
                fault->gva = gva;
            }
            return -EFAULT;
        }

        // TODO: nofault vairant
        memcpy(dst, src + idx, can_write);

        idx += can_write;
        gva += can_write;
        size -= can_write;
    }

    return 0;
}
