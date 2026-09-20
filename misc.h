#ifndef _AC_MISC_H
#define _AC_MISC_H

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include "asm/current.h"
#include "asm/fsgsbase.h"
#include "linux/sched.h"
#include "svm.h"

#define MSRPM_SIZE              8192
#define MSRPM_RANGE0_BASE       0x00000000
#define MSRPM_RANGE0_END        0x00001FFF
#define MSRPM_RANGE1_BASE       0xC0000000
#define MSRPM_RANGE1_END        0xC0001FFF
#define MSRPM_RANGE2_BASE       0xC0010000
#define MSRPM_RANGE2_END        0xC0011FFF
#define MSRPM_OFFSET_RANGE0     0x0000
#define MSRPM_OFFSET_RANGE1     0x0800  /* 2KB */
#define MSRPM_OFFSET_RANGE2     0x1000  /* 4KB */
#define MSRPM_INTERCEPT_READ    0b01
#define MSRPM_INTERCEPT_WRITE   0b10

#define CPUID_MAX_STANDARD_FN_NUMBER_AND_VENDOR_STRING          0x00000000
#define CPUID_PROCESSOR_AND_PROCESSOR_FEATURE_IDENTIFIERS       0x00000001
#define CPUID_PROCESSOR_AND_PROCESSOR_FEATURE_IDENTIFIERS_EX    0x80000001
#define CPUID_SVM_FEATURES                                      0x8000000a
#define CPUID_FN8000_0001_ECX_SVM                   (1UL << 2)
#define CPUID_FN0000_0001_ECX_HYPERVISOR_PRESENT    (1UL << 31)
#define CPUID_FN8000_000A_EDX_NP                    (1UL << 0)

typedef void *(*kallsyms_lookup_name_t)(const char *name);

struct guest_registers {
    u64 rax;
    u64 rcx;
    u64 rdx;
    u64 rbx;
    u64 rbp;
    u64 rsi;
    u64 rdi;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
};

struct ac_insn_info {
    int reg_nr;
    u64  gva;
    int  len;
};

static inline void ac_cpuid(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    asm volatile("cpuid"
                 : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
                 : "a" (leaf), "c" (subleaf)
                 : "memory");
}

void ac_msrpm_set_intercept(u8 *msrpm_va, u32 msr, int flags);
void *ac_msrpm_build(void);
void *ac_find_ksym(const char *name);
bool ac_is_svm_supported(void);
int ac_parse_group7(u8 *buf, int max_len, struct vmcb *vmcb,
                              struct guest_registers *regs, struct ac_insn_info *info);

static inline u64 ac_get_gpr(struct guest_registers *regs, struct vmcb *vmcb, uint idx) {
    switch (idx & 0x0f) {
        case 0:  return vmcb->save.rax;
        case 1:  return regs->rcx;
        case 2:  return regs->rdx;
        case 3:  return regs->rbx;
        case 4:  return vmcb->save.rsp;
        case 5:  return regs->rbp;
        case 6:  return regs->rsi;
        case 7:  return regs->rdi;
        case 8:  return regs->r8;
        case 9:  return regs->r9;
        case 10: return regs->r10;
        case 11: return regs->r11;
        case 12: return regs->r12;
        case 13: return regs->r13;
        case 14: return regs->r14;
        case 15: return regs->r15;
        default: unreachable();
    }
}

static inline struct task_struct *ac_get_current(struct vmcb *vmcb)
{
    struct task_struct *ret;
    u64 gs_base = rdgsbase(); // temporarily pivot to guest's tls
    wrgsbase((vmcb->save.cpl == 3) ? vmcb->save.kernel_gs_base : vmcb->save.gs.base);
    asm volatile("" : "=r" (ret) : "0" (current));
    wrgsbase(gs_base);        // go back to host's tls

    return ret;
}

#endif /* _AC_MISC_H */
