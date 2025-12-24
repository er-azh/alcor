#ifndef _AC_NPT_H
#define _AC_NPT_H

#include <linux/kernel.h>
#include <asm/page.h>

#define NPT_P       (1ULL << 0)
#define NPT_RW      (1ULL << 1)
#define NPT_US      (1ULL << 2)
#define NPT_PS      (1ULL << 7)

#define PHYS_MASK   0x000FFFFFFFFFF000ULL
#define PTE_POOL_SIZE 768

typedef union npt_pml4_entry {
    u64 val;
    struct {
        u64 valid : 1;
        u64 write : 1;
        u64 user : 1;
        u64 pwt : 1;
        u64 pcd : 1;
        u64 accessed : 1;
        u64 ignored_1 : 3;
        u64 avl : 3;
        u64 pfn : 40;
        u64 ignored_2 : 11;
        u64 nx : 1;
    } fields;
} npt_pml4_entry_t, npt_pdp_entry_t;

typedef union npt_pd_entry_2m {
    u64 val;
    struct {
        u64 valid : 1;
        u64 write : 1;
        u64 user : 1;
        u64 pwt : 1;
        u64 pcd : 1;
        u64 accessed : 1;
        u64 dirty : 1;
        u64 large_page : 1;
        u64 global : 1;
        u64 avl : 3;
        u64 pat : 1;
        u64 ignored_1 : 8;
        u64 pfn : 31;
        u64 ignored_2 : 11;
        u64 nx : 1;
    } fields;
} npt_pd_entry_2m_t;

typedef union npt_pte_entry {
    u64 val;
    struct {
        u64 valid : 1;
        u64 write : 1;
        u64 user : 1;
        u64 pwt : 1;
        u64 pcd : 1;
        u64 accessed : 1;
        u64 dirty : 1;
        u64 pat : 1;
        u64 global : 1;
        u64 avl : 3;
        u64 pfn : 40;
        u64 ignored_1 : 11;
        u64 nx : 1;
    } fields;
} npt_pte_entry_t;

typedef union npt_pd_entry_4k {
    u64 val;
    struct {
        u64 valid : 1;
        u64 write : 1;
        u64 user : 1;
        u64 pwt : 1;
        u64 pcd : 1;
        u64 accessed : 1;
        u64 ignored_1 : 1;
        u64 large_page : 1;
        u64 ignored_2 : 4;
        u64 pfn : 40;
        u64 ignored_3 : 11;
        u64 nx : 1;
    } fields;
} npt_pd_entry_4k_t;


struct ac_mapping_tables {
    npt_pml4_entry_t *pml4;
    npt_pdp_entry_t *pdp;
    npt_pd_entry_2m_t *pde[512];

    npt_pte_entry_t *pte_pool[PTE_POOL_SIZE];
    u32 pte_pool_used;

    void *msrpm;
}  __attribute__((packed));

void ac_npt_mask_page(struct ac_mapping_tables *tabs, u64 guest_phys);
struct ac_mapping_tables *ac_npt_build_mapping_tables(void);
void ac_npt_debug_walk(u64 ncr3, u64 fault_phy);
void ac_npt_free_mapping_tables(struct ac_mapping_tables *tab);

#endif /* _AC_NPT_H */
