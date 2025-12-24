#include <linux/kernel.h>
#include <asm/page.h>
#include "mem.h"
#include "misc.h"
#include "npt.h"

static npt_pte_entry_t *ac_npt_alloc_pte_table(struct ac_mapping_tables *tabs) {
    if (tabs->pte_pool_used + 1 > PTE_POOL_SIZE) {
        pr_err("ac_alloc_pte_table: PTE pool overflow.\n");
        return NULL;
    }

    void *ptr = (u8 *)(tabs->pte_pool[tabs->pte_pool_used]);
    tabs->pte_pool_used += 1;
    return (npt_pte_entry_t *)ptr;
}

void ac_npt_mask_page(struct ac_mapping_tables *tabs, u64 guest_phys) {
    u64 pdp_idx = (guest_phys >> 30) & 0x1FF;
    u64 pde_idx = (guest_phys >> 21) & 0x1FF;
    u64 pte_idx = (guest_phys >> 12) & 0x1FF;

    npt_pd_entry_2m_t *pde_entry = &tabs->pde[pdp_idx][pde_idx];

    // TODO: get rid of the bitfield structs entirely
    if (pde_entry->fields.large_page) {
        npt_pte_entry_t *new_pte_table = ac_npt_alloc_pte_table(tabs);
        if (!new_pte_table) return;

        u64 base_pfn_4k = (u64)pde_entry->fields.pfn << 9;

        for (int i = 0; i < 512; i++) {
            new_pte_table[i].fields.valid = 1;
            new_pte_table[i].fields.write = 1;
            new_pte_table[i].fields.user = 1;
            new_pte_table[i].fields.pfn = base_pfn_4k + i;
        }

        npt_pd_entry_4k_t *pde_4k = (npt_pd_entry_4k_t *)pde_entry;
        pde_4k->val = 0;
        pde_4k->fields.valid = 1;
        pde_4k->fields.write = 1;
        pde_4k->fields.user = 1;
        pde_4k->fields.large_page = 0;
        pde_4k->fields.pfn = __pa(new_pte_table) >> PAGE_SHIFT;
    }

    npt_pd_entry_4k_t *pde_4k = (npt_pd_entry_4k_t *)pde_entry;
    npt_pte_entry_t *pte_table = __va(pde_4k->fields.pfn << PAGE_SHIFT);

    pte_table[pte_idx].val = 0;
}

struct ac_mapping_tables *ac_npt_build_mapping_tables(void) {
    struct ac_mapping_tables *tab;
    int i, j;

    tab = ac_alloc_pages(get_order(sizeof(struct ac_mapping_tables)));
    if (!tab) return NULL;

    tab->msrpm = ac_msrpm_build();
    if (!tab->msrpm) goto error;

    for (i = 0; i < PTE_POOL_SIZE; i++) {
        tab->pte_pool[i] = (void *)ac_alloc_page();
        if (!tab->pte_pool[i]) goto error;
    }

    tab->pml4 = (void *)ac_alloc_page();
    if (!tab->pml4) goto error;

    tab->pdp = (void *)ac_alloc_page();
    if (!tab->pdp) goto error;

    tab->pml4[0].val = (__pa(tab->pdp) & PHYS_MASK) | NPT_US | NPT_RW | NPT_P;

    for (i = 0; i < 512; i++) {
        tab->pde[i] = (void *)ac_alloc_page();
        if (!tab->pde[i]) goto error;
        tab->pdp[i].val = (__pa(tab->pde[i]) & PHYS_MASK) | NPT_US | NPT_RW | NPT_P;

        for (j = 0; j < 512; j++) {
            u64 phys_addr = ((u64)i << 30) + ((u64)j << 21);
            phys_addr &= 0x000FFFFFFFF00000ULL;

            tab->pde[i][j].val = phys_addr | NPT_PS | NPT_US | NPT_RW | NPT_P;
        }
    }

    ac_npt_mask_page(tab, __pa(tab->pml4));
    ac_npt_mask_page(tab, __pa(tab->pdp));
    for (i = 0; i < 512; i++) {
        ac_npt_mask_page(tab, __pa(tab->pde[i]));
    }

    for (i = 0; i < PTE_POOL_SIZE; i++) {
        ac_npt_mask_page(tab, __pa(tab->pte_pool[i]));
    }

    return tab;
error:
    return NULL;
}


void ac_npt_debug_walk(u64 ncr3, u64 fault_phy) {
    u64 *tbl;
    u64 entry, next_phy;
    int idx;

    u64 pml4_base = ncr3 & 0x000FFFFFFFFFF000ULL;
    tbl = (u64 *)__va(pml4_base);

    printk("nCR3: 0x%llx (va=0x%p)\n", ncr3, tbl);

    idx = (fault_phy >> 39) & 0x1FF;
    entry = tbl[idx];
    printk("L4 [idx %d]: 0x%llx\n", idx, entry);

    if (!(entry & NPT_P)) { printk(KERN_ERR " -> entry not marked as present. end of walk.\n"); return; }
    if (entry & NPT_PS) { printk(KERN_ERR " -> PS bit set at L4. error.!\n"); }

    next_phy = entry & 0x000FFFFFFFFFF000ULL;
    tbl = (u64 *)__va(next_phy);
    idx = (fault_phy >> 30) & 0x1FF;
    entry = tbl[idx];

    printk("L3 [idx %d]: 0x%llx\n", idx, entry);

    if (!(entry & NPT_P)) { printk(KERN_ERR " -> entry not marked as present. end of walk.\n"); return; }

    if (entry & NPT_PS) {
        printk(KERN_ERR " -> 1GB page. end of walk.\n");
        if (entry & 0x3FFFE000ULL) printk(KERN_ERR "reserved bits 13-29 set.\n");
        return;
    }

    next_phy = entry & 0x000FFFFFFFFFF000ULL;
    tbl = (u64 *)__va(next_phy);
    idx = (fault_phy >> 21) & 0x1FF;
    entry = tbl[idx];

    printk("L2 [idx %d]: 0x%llx\n", idx, entry);

    if (!(entry & NPT_P)) { printk(KERN_ERR " -> entry not marked as present. end of walk.\n"); return; }

    if (entry & NPT_PS) {
        printk(KERN_ERR " -> 2MB page. end of walk.\n");
        if (entry & 0x1FE000ULL) printk(KERN_ERR "reserved bits 13-20 set.\n");
        return;
    }

    next_phy = entry & 0x000FFFFFFFFFF000ULL;
    tbl = (u64 *)__va(next_phy);
    idx = (fault_phy >> 12) & 0x1FF;
    entry = tbl[idx];

    printk("L1 [idx %d]: 0x%llx\n", idx, entry);
}


void ac_npt_free_mapping_tables(struct ac_mapping_tables *tab) {
    int i;
    if (!tab) return;

    if (tab->msrpm) ac_free_pages(tab->msrpm, get_order(8192));
    tab->msrpm = NULL;

    if (tab->pml4)  ac_free_page(tab->pml4);
    tab->pml4 = NULL;

    if (tab->pdp)   ac_free_page(tab->pdp);
    tab->pdp = NULL;

    for (i = 0; i < 512; i++) {
        if (tab->pde[i]) {
            ac_free_page(tab->pde[i]);
            tab->pde[i] = NULL;
        }
    }

    for (i = 0; i < PTE_POOL_SIZE; i++) {
        if (tab->pte_pool[i]) {
            ac_free_page(tab->pte_pool[i]);
            tab->pte_pool[i] = NULL;
        }
    }
    ac_free_pages(tab, get_order(sizeof(struct ac_mapping_tables)));
}
