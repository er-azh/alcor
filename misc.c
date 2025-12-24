#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/kprobes.h>
#include <asm/current.h>

#include "mem.h"
#include "misc.h"

void ac_msrpm_set_intercept(u8 *msrpm_va, u32 msr, int flags) {
    u32 offset;
    u32 range_offset;

    if (msr >= MSRPM_RANGE0_BASE && msr <= MSRPM_RANGE0_END) {
        offset = MSRPM_OFFSET_RANGE0;
        range_offset = msr - MSRPM_RANGE0_BASE;
    }
    else if (msr >= MSRPM_RANGE1_BASE && msr <= MSRPM_RANGE1_END) {
        offset = MSRPM_OFFSET_RANGE1;
        range_offset = msr - MSRPM_RANGE1_BASE;
    }
    else if (msr >= MSRPM_RANGE2_BASE && msr <= MSRPM_RANGE2_END) {
        offset = MSRPM_OFFSET_RANGE2;
        range_offset = msr - MSRPM_RANGE2_BASE;
    }
    else {
        pr_warn("unsupported MSR range\n");
        return;
    }

    u32 byte_pos = offset + (range_offset / 4);
    u32 bit_pos = (range_offset * 2) % 8;

    if (flags & MSRPM_INTERCEPT_READ) {
        msrpm_va[byte_pos] |= (1 << bit_pos);
    }
    if (flags & MSRPM_INTERCEPT_WRITE) {
        msrpm_va[byte_pos] |= (1 << (bit_pos + 1));
    }
}

void* ac_msrpm_build(void) {
    void *msrpm;

    msrpm = ac_alloc_pages(get_order(8192));
    if (!msrpm) {
        pr_err("failed to allocate MSRPM page.\n");
        return NULL;
    }
    return msrpm;
}

void *ac_find_ksym(const char *name) {
    struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
    kallsyms_lookup_name_t lookup;
    void *addr;

    if (register_kprobe(&kp) < 0) {
        pr_err("failed to register kprobe for kallsyms_lookup_name\n");
        return NULL;
    }
    lookup = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);

    addr = lookup(name);
    if (!addr) {
        pr_err("failed to find symbol %s\n", name);
    }

    return addr;
}


bool ac_is_svm_supported(void)
{
    u32 eax, ebx, ecx, edx;
    u64 vmcr;

    cpuid(CPUID_MAX_STANDARD_FN_NUMBER_AND_VENDOR_STRING, &eax, &ebx, &ecx, &edx);
    if (ebx != 0x68747541 || /* "Auth" */
        edx != 0x69746e65 || /* "enti" */
        ecx != 0x444d4163)   /* "cAMD" */
    {
        pr_err("only AMD cpus are supported.\n");
        return false;
    }

    // CPUID 0x80000001, ECX Bit 2
    cpuid(CPUID_PROCESSOR_AND_PROCESSOR_FEATURE_IDENTIFIERS_EX, &eax, &ebx, &ecx, &edx);
    if (!(ecx & CPUID_FN8000_0001_ECX_SVM)) {
        pr_err("svm is not supported or enabled.\n");
        return false;
    }

    // CPUID 0x8000000A, EDX Bit 0
    cpuid(CPUID_SVM_FEATURES, &eax, &ebx, &ecx, &edx);
    if (!(edx & CPUID_FN8000_000A_EDX_NP)) {
        pr_err("nested paging (NPT) is not supported.\n");
        return false;
    }

    // MSR 0xC0010114,
    rdmsrl(MSR_VM_CR, vmcr);
    if (vmcr & SVM_VM_CR_SVM_DIS_MASK) {
        if (vmcr & SVM_VM_CR_SVM_LOCK_MASK) {
            pr_err("svm is disabled and locked.\n");
            return false;
        } else {
            pr_err("svm is disabled but unlocked. attempting to enable...\n");

            wrmsrl(MSR_VM_CR, vmcr & ~SVM_VM_CR_SVM_DIS_MASK);

            rdmsrl(MSR_VM_CR, vmcr);
            if (vmcr & SVM_VM_CR_SVM_DIS_MASK) {
                pr_err("failed to enable svm.\n");
                return false;
            }
            pr_info("successfully enabled svm.\n");
        }
    }

    return true;
}

static inline bool ac_is_segment_override(u8 b) {
    return (b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 ||
            b == 0x64 || b == 0x65);
}

int ac_parse_group7(u8 *buf, int max_len, struct vmcb *vmcb,
                              struct guest_registers *regs, struct ac_insn_info *info)
{
    int i = 0;
    u8 b, modrm, sib, rex = 0, seg_ovr = 0;
    bool addr_ovr = false;
    bool long_mode = (vmcb->save.efer & EFER_LMA);

    while (i < max_len) {
        b = buf[i];

        if (ac_is_segment_override(b)) {
            seg_ovr = b;
            i++;
        } else if (b == 0x67) {
            addr_ovr = true;
            i++;
        } else if (b == 0x66) { // operand sz
            i++;
        } else if ((b & 0xF0) == 0x40 && long_mode) { // rex
            rex = b;
            i++;
            break;
        } else {
            break;
        }
    }

    if (i + 1 >= max_len) return -EINVAL;
    if (buf[i] != 0x0f || buf[i+1] != 0x01) return -EINVAL;
    i += 2;

    if (i >= max_len) return -EINVAL;
    modrm = buf[i++];

    int mod = (modrm >> 6) & 3;
    int reg = (modrm >> 3) & 7; // 0=SGDT, 1=SIDT
    int rm  = modrm & 7;

    if (reg > 1) return -EINVAL;
    if (mod == 3) return -EINVAL;

    info->reg_nr = reg;

    u64 addr = 0;
    int32_t disp = 0;
    bool uses_rip = false;

    int base_reg_idx = rm | ((rex & 1) << 3);
    bool has_sib = (rm == 4);

    if (has_sib) {
        if (i >= max_len) return -EINVAL;
        sib = buf[i++];

        int scale = 1 << ((sib >> 6) & 3);
        int index = (sib >> 3) & 7;
        int base  = sib & 7;

        int index_reg_idx = index | ((rex & 2) << 2);
        int sib_base_idx  = base  | ((rex & 1) << 3);

        if (base == 5 && mod == 0) {
            addr = 0;
        } else {
            addr += ac_get_gpr(regs, vmcb, sib_base_idx);
        }

        if (index != 4) {
             addr += ac_get_gpr(regs, vmcb, index_reg_idx) * scale;
        }

        base_reg_idx = sib_base_idx;
        if (base == 5 && mod == 0) base_reg_idx = -1;

    } else {
        if (mod == 0 && rm == 5) {
            if (long_mode) {
                uses_rip = true;
            } else {
                addr = 0;
            }
        } else {
            addr = ac_get_gpr(regs, vmcb, base_reg_idx);
        }
    }

    if (mod == 1) {
        if (i >= max_len) return -EINVAL;
        disp = (int8_t)buf[i++];
    } else if (mod == 2 || (mod == 0 && rm == 5) || (has_sib && (sib & 7) == 5 && mod == 0)) {
        if (i + 4 > max_len) return -EINVAL;
        disp = *(int32_t *)&buf[i];
        i += 4;
    }

    if (uses_rip) {
        addr = vmcb->save.rip + i + disp;
    } else {
        addr += disp;
    }

    if (long_mode && addr_ovr) {
        addr &= 0xFFFFFFFF;
    }

    u64 seg_base = 0;
    if (seg_ovr) {
        switch (seg_ovr) {
            case 0x2E: seg_base = vmcb->save.cs.base; break;
            case 0x36: seg_base = vmcb->save.ss.base; break;
            case 0x3E: seg_base = vmcb->save.ds.base; break;
            case 0x26: seg_base = vmcb->save.es.base; break;
            case 0x64: seg_base = vmcb->save.fs.base; break;
            case 0x65: seg_base = vmcb->save.gs.base; break;
        }
    } else {
        bool is_stack_reg = (base_reg_idx == 4 || base_reg_idx == 5);

        if (is_stack_reg && !uses_rip) {
            seg_base = vmcb->save.ss.base;
        } else {
            seg_base = vmcb->save.ds.base;
        }
    }

    info->gva = seg_base + addr;
    info->len = i;
    return 0;
}
