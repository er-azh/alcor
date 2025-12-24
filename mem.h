#ifndef _AC_MEM_H
#define _AC_MEM_H
#include <linux/kernel.h>

#define PG_NEXT_ADDR(entry) ((entry) & 0x000FFFFFFFFFF000ULL)
#define PF_ERR_P      (1 << 0)
#define PF_ERR_W      (1 << 1)
#define PF_ERR_U      (1 << 2)
#define PF_ERR_RSV    (1 << 3)
#define PF_ERR_ID     (1 << 4)

struct ac_mm_fault {
    int fault;
    u64 gva;
};

enum ac_mm_access {
    AC_MM_READ = 1 << 0,
    AC_MM_WRITE = 1 << 1,
    AC_MM_FETCH = 1 << 2,
};

void *ac_alloc_page(void);
void ac_free_page(void *page);
void *ac_alloc_pages(unsigned int order);
void ac_free_pages(void *page, unsigned int order);
void *ac_mm_walk_pagetable(u64 cr3, u64 gva, enum ac_mm_access access, int cpl, int *fault_code);
int ac_mm_copy_from_guest(u8 *dst, size_t size, u64 cr3, u64 gva, enum ac_mm_access access, int cpl, struct ac_mm_fault *fault);
int ac_mm_copy_to_guest(u8 *src, size_t size, u64 cr3, u64 gva, enum ac_mm_access access, int cpl, struct ac_mm_fault *fault);

#endif /* _AC_MEM_H */
