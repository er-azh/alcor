#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/sched/task.h>
#include <linux/miscdevice.h>
#include <linux/xarray.h>
#include <linux/pid.h>
#include <linux/syscore_ops.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <asm/io.h>
#include <asm/desc.h>
#include <asm/trapnr.h>
#include <asm/msr-index.h>

#include "mem.h"
#include "svm.h"
#include "npt.h"
#include "misc.h"
#include "hypercalls.h"
#include "api.h"


#define AC_VMEXIT_OK 0
#define AC_VMEXIT_DEVIRTUALIZE 1

#define AC_HV_STACK_SIZE 16384
#define AC_DUMMY_GDT_BASE 0xfffffffffffe0000ULL
#define AC_DUMMY_IDT_BASE 0xffffffffffff0000ULL

#define SET_INTERCEPT(vmcb, intercept) set_bit(intercept, (ulong*)&vmcb->control.intercept_cr_read);
#define CLEAR_INTERCEPT(vmcb, intercept) clear_bit(intercept, (ulong*)&vmcb->control.intercept_cr_read);

struct host_stack_layout {
    u64 guest_vmcb_pa;        // [rsp + 0x00]
    u64 host_vmcb_pa;         // [rsp + 0x08]
    struct ac_vcpu_ctx *self; // [rsp + 0x10]
    u64 *vmcb_rsp_ptr;        // [rsp + 0x18]
    struct vmcb *guest_vmcb;  // [rsp + 0x20]
};

struct ac_vcpu_ctx {
    struct {
        uint8_t stack[AC_HV_STACK_SIZE - sizeof(struct host_stack_layout)];
        struct host_stack_layout top;
    } stack_layout;

    struct vmcb *guest_vmcb;

    u64 last_trapped_exit;
    u64 shadow_hsave_pa;
    atomic_long_t is_virtualized;
    void *host_vmcb;
    void *host_state_area;
};


extern int ac_hv_launch(void* hv_stack, u64 pg);
extern int ac_hv_devirtualize(void);
extern void ac_hv_resume_guest(void);
u32 ac_hv_vmexit(struct guest_registers* regs);

static struct ac_mapping_tables *mapping_tables = NULL;
struct mm_struct *root_pgd;
static bool used_to_have_umip = false;
DEFINE_PER_CPU(struct ac_vcpu_ctx *, host_vcpu);
static DEFINE_XARRAY(hooklist);

typedef bool (*ptrace_may_access_t)(struct task_struct *task, unsigned int mode);
static ptrace_may_access_t real_ptrace_may_access = NULL;

static inline bool ac_exception_has_error_code(u8 vector) {
    static const u32 mask = (1 << X86_TRAP_DF) | (1 << X86_TRAP_TS) |
                            (1 << X86_TRAP_NP) | (1 << X86_TRAP_SS) |
                            (1 << X86_TRAP_GP) | (1 << X86_TRAP_PF) |
                            (1 << X86_TRAP_AC) | (1 << X86_TRAP_MC);
    return (vector < 32) && (mask & (1 << vector));
}

static void ac_inject_exception(struct vmcb *vmcb, u8 vector, u32 error_code)
{
    union {
        u64 val;
        struct {
            u64 vector : 8;             // [0:7]
            u64 type : 3;               // [8:10]
            u64 error_code_valid : 1;   // [11]
            u64 reserved : 19;          // [12:30]
            u64 valid : 1;              // [31]
            u64 error_code : 32;        // [32:63]
        };
    } event;

    event.val = 0;
    event.vector = vector;
    event.type = 3;
    event.error_code_valid = ac_exception_has_error_code(vector) ? 1 : 0;
    event.valid = 1;
    if (event.error_code_valid) event.error_code = error_code;

    vmcb->control.eventinj = event.val;
}


static void ac_debug_serial(const char* msg) {
    for (; *msg; msg++) {
        outb(*msg, 0x3f8);
    }
    outb('\n', 0x3f8);
}

static void ac_hv_handle_msr(struct guest_registers *regs, struct vmcb *vmcb) {
    struct ac_vcpu_ctx *vcpu;
    u32 msr = regs->rcx & U32_MAX;
    bool is_write = vmcb->control.exitinfo1 != 0;
    union {
        struct {
            u32 high;
            u32 low;
        };
        u64 quad;
    } value;

    if (vmcb->save.cpl != 0) {
        ac_inject_exception(vmcb, X86_TRAP_GP, 0);
        return;
    }

    value.low = regs->rax & U32_MAX;
    value.high = regs->rdx & U32_MAX;

    if (msr == MSR_EFER) {
        if (is_write) {
            vmcb->save.efer = value.quad | EFER_SVME;
        } else {
            value.quad = vmcb->save.efer & ~EFER_SVME;
            regs->rax = value.low;
            regs->rdx = value.high;
        }
    } else if (unlikely(msr == MSR_VM_HSAVE_PA)) {
        vcpu = this_cpu_read(host_vcpu); // NOLINT(bugprone-sizeof-expression)

        if (is_write) {
            vcpu->shadow_hsave_pa = value.quad;
        } else {
            value.quad = vcpu->shadow_hsave_pa;
            regs->rax = value.low;
            regs->rdx = value.high;
        }
    } else {
        if ((msr <= MSRPM_RANGE0_END && msr >= MSRPM_RANGE0_BASE)
            || (msr <= MSRPM_RANGE1_END && msr >= MSRPM_RANGE1_BASE)
            || (msr <= MSRPM_RANGE2_END && msr >= MSRPM_RANGE2_BASE)) { // oor msrs will always vmexit
            pr_warn("msr access but wasn't requested msr=0x%x, is_write=%d, val=%llx", msr, is_write, value.quad);
        }
        if (is_write) {
            wrmsrq(msr, value.quad);
        } else {
            rdmsrq(msr, value.quad);
            regs->rax = value.low;
            regs->rdx = value.high;
        }
    }

    vmcb->save.rip = vmcb->control.nrip;
}


static void ac_hv_handle_cpuid(struct guest_registers *regs, struct vmcb *vmcb) {
    u32 eax, ebx, ecx, edx;
    u32 leaf = regs->rax & U32_MAX;
    u32 subleaf = regs->rcx & U32_MAX;
    struct task_struct *task;
    struct ac_hook_data *hook;

    ac_cpuid(leaf, subleaf, &eax, &ebx, &ecx, &edx);

    switch (leaf) {
        case 0x1:
            ecx |= (1UL << 31);
            break;

        case 0x80000001:
            ecx &= ~(1UL << 2);
            break;

        case 0x8000000A:
            eax = ebx = ecx = edx = 0;
            break;
    }

    if (vmcb->save.cpl == 3) {
        task = ac_get_current(vmcb);
        hook = xa_load(&hooklist, task->tgid);
        if (likely(!hook)) goto done;

        for (int i = 0; i < hook->cpuid_count; i++) {
            if (hook->cpuid_overrides[i].leaf == leaf &&
                ((!(hook->cpuid_overrides[i].flags & AC_CPUID_HAS_SUBLEAF_MASK)) ||
                    hook->cpuid_overrides[i].subleaf == subleaf)) {

                if (hook->cpuid_overrides[i].flags & AC_CPUID_EAX_MASK)
                    eax = hook->cpuid_overrides[i].regs[AC_CPUID_EAX];
                if (hook->cpuid_overrides[i].flags & AC_CPUID_EBX_MASK)
                    ebx = hook->cpuid_overrides[i].regs[AC_CPUID_EBX];
                if (hook->cpuid_overrides[i].flags & AC_CPUID_ECX_MASK)
                    ecx = hook->cpuid_overrides[i].regs[AC_CPUID_ECX];
                if (hook->cpuid_overrides[i].flags & AC_CPUID_EDX_MASK)
                    edx = hook->cpuid_overrides[i].regs[AC_CPUID_EDX];

                break;
            }
        }

        if (hook->log_cpuid) {
            pr_info("pid=%d leaf=%x subleaf=%x eax=%x ebx=%x ecx=%x edx=%x\n",
                   task->tgid, leaf, subleaf, eax, ebx, ecx, edx);
        }
    }

done:
    regs->rax = eax;
    regs->rbx = ebx;
    regs->rcx = ecx;
    regs->rdx = edx;

    vmcb->save.rip = vmcb->control.nrip;
}

static inline void ac_halt_forever(void) {
    while (1) { asm volatile("hlt"); }
}

static inline void ac_triple_fault(void) {
    asm volatile("pushq $0; pushq $0; lidt (%rsp); int3");
    ac_halt_forever();
}

static inline void ac_hv_trap_step(struct vmcb *vmcb) {
    struct ac_vcpu_ctx *vcpu = this_cpu_read(host_vcpu); // NOLINT(bugprone-sizeof-expression)

    CLEAR_INTERCEPT(vmcb, vmcb->control.exitcode);
    SET_INTERCEPT(vmcb, VMEXIT_EXCEPTION_DB);

    vcpu->last_trapped_exit = vmcb->control.exitcode;
    vmcb->save.rflags |= X86_EFLAGS_TF;
}

static void ac_hv_handle_trap(struct vmcb *vmcb, struct guest_registers *regs) {
    struct ac_vcpu_ctx *vcpu = this_cpu_read(host_vcpu); // NOLINT(bugprone-sizeof-expression)
    CLEAR_INTERCEPT(vmcb, VMEXIT_EXCEPTION_DB);
    SET_INTERCEPT(vmcb, vcpu->last_trapped_exit);

    vmcb->save.rflags &= ~X86_EFLAGS_TF;
}

static inline long ac_forward_hypercall_kvm(long id, long arg1, long arg2, long arg3, long arg4) {
    long ret;
    asm volatile("vmmcall"
                 : "=a"(ret)
                 : "a"(id), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
                 : "memory");
    return ret;
}

static void ac_hv_handle_segment(struct vmcb *vmcb, struct guest_registers *regs) {
    char buf[16];
    u16 dt_size;
    u64 dt_base;
    size_t write_size;
    struct ac_hook_data *hook;
    struct task_struct *task;
    struct ac_insn_info insn;
    struct ac_mm_fault fault;

    if (vmcb->control.exitcode != VMEXIT_GDTR_READ &&
        vmcb->control.exitcode != VMEXIT_IDTR_READ) {
        panic("ac_hv_handle_segment: unknown vmexit code %llx", vmcb->control.exitcode);
        ac_halt_forever();
    }

    if (vmcb->save.cpl != 3) {
        return ac_hv_trap_step(vmcb);
    }

    task = ac_get_current(vmcb);
    hook = xa_load(&hooklist, task->tgid);

    pr_info("%s read from cpl=3, walking pagetables: 0x%llx, pid=%d\n",
        vmcb->control.exitcode == VMEXIT_GDTR_READ ? "gdtr" : "idtr",
        vmcb->save.rip, task->tgid);

    if (ac_mm_copy_from_guest(buf,
            min_t(size_t, sizeof(buf), (size_t)(vmcb->control.nrip - vmcb->save.rip)),
            vmcb->save.cr3, vmcb->save.rip, AC_MM_FETCH, vmcb->save.cpl, &fault) != 0) {
        pr_warn("page table walk faulted while fetching at 0x%llx\n", vmcb->save.rip);
        vmcb->save.cr2 = fault.gva;
        ac_inject_exception(vmcb, X86_TRAP_PF, fault.fault);
        return;
    }

    if (ac_parse_group7(buf, sizeof(buf), vmcb, regs, &insn) < 0) {
        pr_warn("decode failed at 0x%llx\n", vmcb->save.rip);
        print_hex_dump(KERN_WARNING, "ac_hv_handle_segment: ", DUMP_PREFIX_OFFSET, 16,
            1, buf, min_t(size_t, sizeof(buf), (size_t)(vmcb->control.nrip - vmcb->save.rip)), false);
        ac_inject_exception(vmcb, X86_TRAP_UD, 0);
        return;
    }

    switch (insn.reg_nr) {
        case 0: // sgdt
            if (hook && hook->segment_flags & AC_GDTR_HOOK_MASK) {
                dt_base = hook->segments_overrides[AC_GDTR_HOOK].address;
                dt_size = hook->segments_overrides[AC_GDTR_HOOK].size;
            } else {
                dt_base = AC_DUMMY_GDT_BASE;
                dt_size = 0;
            }
            break;
        case 1: // sidt
            if (hook && hook->segment_flags & AC_IDTR_HOOK_MASK) {
                dt_base = hook->segments_overrides[AC_IDTR_HOOK].address;
                dt_size = hook->segments_overrides[AC_IDTR_HOOK].size;
            } else {
                dt_base = AC_DUMMY_IDT_BASE;
                dt_size = 0;
            }
            break;
        default:
            pr_warn("invalid group 7 register number %d\n", insn.reg_nr);
            ac_inject_exception(vmcb, X86_TRAP_UD, 0);
            return;
    }

    memset(&buf, 0, sizeof(buf));
    memcpy(&buf, &dt_size, sizeof(u16));
    if (vmcb->save.efer & EFER_LME) {
        write_size = sizeof(u16) + sizeof(u64);
        memcpy(&buf[2], &dt_base, sizeof(u64));
    } else {
        write_size = sizeof(u16) + sizeof(u32);
        memcpy(&buf[2], &dt_base, sizeof(u32));
    }

    if (ac_mm_copy_to_guest((u8*)&buf, write_size, vmcb->save.cr3, insn.gva, AC_MM_WRITE, vmcb->save.cpl, &fault) != 0) {
        pr_warn("page table walk faulted while writing sgdt result at 0x%llx\n", vmcb->save.rip);
        vmcb->save.cr2 = fault.gva;
        ac_inject_exception(vmcb, X86_TRAP_PF, fault.fault);
        return;
    }

    vmcb->save.rip += insn.len;
    pr_info("result written at 0x%llx\n", insn.gva);
    return;
}

u32 notrace ac_hv_vmexit(struct guest_registers* regs) {
    char buf[256];
    u64 vmcb_pa = regs->rax, regval;
    struct vmcb *vmcb = (struct vmcb *)__va(vmcb_pa);
    regs->rax = vmcb->save.rax;

    switch ((int)vmcb->control.exitcode) {
        case VMEXIT_MSR:
            ac_hv_handle_msr(regs, vmcb);
            break;
        case VMEXIT_CPUID:
            ac_hv_handle_cpuid(regs, vmcb);
            break;
        case VMEXIT_NPF:
            snprintf(buf, sizeof(buf), "[HV-FATAL] nested page fault (ec=%llx, phy=%llx)", vmcb->control.exitinfo1, vmcb->control.exitinfo2);
            ac_debug_serial(buf);
            ac_npt_debug_walk(vmcb->control.n_cr3, vmcb->control.exitinfo2);
            ac_halt_forever();
            break;
        case VMEXIT_VMMCALL:
            if (vmcb->save.cpl == 0 && regs->rdi == DEVIRTUALIZE_MAGIC) {
                pr_info("devirtualization request from %llx\n", vmcb->control.nrip);
                regs->rax = vmcb_pa;
                regs->rdi = vmcb->save.rflags;
                regs->r10 = vmcb->save.efer;
                regs->r11 = vmcb->save.cr4;
                regs->r12 = vmcb->save.cr3;
                regs->r13 = vmcb->save.cr0;
                regs->r15 = vmcb->save.rsp;
                regs->r14 = vmcb->control.nrip;

                if (used_to_have_umip) regs->r11 |= X86_CR4_UMIP;
                return AC_VMEXIT_DEVIRTUALIZE;
            } else if (vmcb->save.cpl == 0) {
                pr_info("vmmcall from cpl=0, rax=0x%llx. treating as kvm hypercall.\n", regs->rax);
                regs->rax = ac_forward_hypercall_kvm(regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rsi);
                vmcb->save.rip = vmcb->control.nrip;
                break;
            } else {
                ac_inject_exception(vmcb, X86_TRAP_UD, 0);
                break;
            }
        case VMEXIT_GDTR_READ:
        case VMEXIT_IDTR_READ:
            ac_hv_handle_segment(vmcb, regs);
            break;
        case VMEXIT_EXCEPTION_DB:
            ac_hv_handle_trap(vmcb, regs);
            break;
        case VMEXIT_VMRUN:
            pr_warn("vmrun intercepted, injecting UD.\n");
            ac_inject_exception(vmcb, X86_TRAP_UD, 0);
            break;
        case VMEXIT_CR4_WRITE:
            regval = ac_get_gpr(regs, vmcb, vmcb->control.exitinfo1);
            vmcb->save.cr4 = regval & ~X86_CR4_UMIP;
            vmcb->save.rip = vmcb->control.nrip;
            break;
        case VMEXIT_SHUTDOWN:
            sprintf(buf, "[HV-FATAL] shutdown (e1=0x%llx, e2=0x%llx)", vmcb->control.exitinfo1, vmcb->control.exitinfo2);
            ac_debug_serial(buf);
            ac_triple_fault();
            break;
        case VMEXIT_INVALID:
            ac_debug_serial("[HV-FATAL] invalid vmcb state");
            ac_triple_fault();
            break;
        default:
            snprintf(buf, sizeof(buf), "[HV-FATAL] unknown exitcode: 0x%llx (info1=%llx, info2=%llx)",
                vmcb->control.exitcode, vmcb->control.exitinfo1, vmcb->control.exitinfo2);

            ac_debug_serial(buf);
            ac_halt_forever();
    }

    vmcb->control.vmcb_clean = 0;
    vmcb->save.rax = regs->rax;
    regs->rax = vmcb_pa;
    return AC_VMEXIT_OK;
}

static int ac_free_vcpu_ctx(struct ac_vcpu_ctx *ctx) {
    if (!ctx) return 0;

    ac_free_page(ctx->guest_vmcb);
    ac_free_page(ctx->host_vmcb);
    ac_free_page(ctx->host_state_area);

    ac_free_pages(ctx, get_order(sizeof(struct ac_vcpu_ctx)));
    return 0;
}

static struct ac_vcpu_ctx* ac_allocate_vcpu_ctx(struct ac_mapping_tables *tab) {
    struct ac_vcpu_ctx *ctx = ac_alloc_pages(get_order(sizeof(struct ac_vcpu_ctx)));
    if (!ctx) {
        pr_err("failed to allocate memory.\n");
        return NULL;
    }

    ctx->guest_vmcb = (void *)ac_alloc_pages(get_order(2 * sizeof(struct vmcb)));
    if (!ctx->guest_vmcb) goto error;
    ac_npt_mask_page(tab, __pa(ctx->guest_vmcb));

    ctx->host_vmcb = ctx->guest_vmcb + 1;
    ac_npt_mask_page(tab, __pa(ctx->host_vmcb));

    ctx->host_state_area = (void *)ac_alloc_page();
    if (!ctx->host_state_area) goto error;
    ac_npt_mask_page(tab, __pa(ctx->host_state_area));

    return ctx;

error:
    pr_err("failed to allocate mapping tables.\n");
    ac_free_vcpu_ctx(ctx);
    return NULL;
}

static inline void ac_copy_segment(struct desc_struct *gdt, u16 sel, struct vmcb_segment *dest) {
    struct desc_struct desc = gdt[sel >> 3];

    dest->selector = sel;
    dest->limit = desc.limit0 | ((u32)desc.limit1 << 16);
    dest->base = desc.base0 | ((u32)desc.base1 << 16) | ((u32)desc.base2 << 24);
    dest->attrib.type = desc.type;
    dest->attrib.s = desc.s;
    dest->attrib.dpl = desc.dpl;
    dest->attrib.p = desc.p;
    dest->attrib.avl = desc.avl;
    dest->attrib.l = desc.l;
    dest->attrib.d = desc.d;
    dest->attrib.g = desc.g;
    dest->attrib.reserved1 = 0;
}

static inline void ac_vmsave(struct vmcb *vmcb) {
    u64 guest_vmcb_pa = __pa(vmcb);
    asm volatile("vmsave %%rax"
                 : "+m" (*vmcb)
                 : "a" (guest_vmcb_pa)
                 : "memory");
}

static void ac_devirtualize_core(void *info) {
    struct ac_vcpu_ctx *vcpu;
    int cpu = smp_processor_id();
    vcpu = this_cpu_read(host_vcpu); // NOLINT(bugprone-sizeof-expression)
    if (!raw_atomic_long_cmpxchg(&vcpu->is_virtualized, true, false)) {
        pr_err("failed to set is_virtualized, already devirtualized?");
        dump_stack();
        return;
    }
    pr_err("core %d: requesting devirtualization.\n", cpu);
    ac_hv_devirtualize();
    pr_err("core %d: received control back in root mode.\n", cpu);
    wrmsrq(MSR_VM_HSAVE_PA, vcpu->shadow_hsave_pa);
    return;
}

static void ac_hijack_core(void *info) {
    struct ac_vcpu_ctx *vcpu;
    int cpu = smp_processor_id();
    struct vmcb *vmcb;
    struct desc_ptr gdtr, idtr;
    struct desc_struct *gdt;
    u64 pml4_pa, msrpm_pa, guest_vmcb_pa, host_vmcb_pa, host_state_pa, tmpmsr;
    u16 cs, ds, es, ss;

    vcpu = this_cpu_read(host_vcpu); // NOLINT(bugprone-sizeof-expression)
    if (raw_atomic_long_cmpxchg(&vcpu->is_virtualized, false, true)) {
        pr_err("failed to set is_virtualized, already virtualized?");
        dump_stack();
        return;
    }

    rdmsrq(MSR_EFER, tmpmsr);
    wrmsrq(MSR_EFER, tmpmsr | EFER_SVME);

    vmcb = vcpu->guest_vmcb;

    pml4_pa = __pa(mapping_tables->pml4);
    msrpm_pa = __pa(mapping_tables->msrpm);
    guest_vmcb_pa = __pa(vcpu->guest_vmcb);
    host_vmcb_pa = __pa(vcpu->host_vmcb);
    host_state_pa = __pa(vcpu->host_state_area);

    pr_err("core %d: hijacking core, vcpu_ctx=0x%p.\n", cpu, vcpu);

    native_store_gdt(&gdtr);
    store_idt(&idtr);

    SET_INTERCEPT(vmcb, VMEXIT_VMRUN);
    SET_INTERCEPT(vmcb, VMEXIT_VMMCALL);
    SET_INTERCEPT(vmcb, VMEXIT_CPUID);
    SET_INTERCEPT(vmcb, VMEXIT_SHUTDOWN);
    SET_INTERCEPT(vmcb, VMEXIT_MSR);

    SET_INTERCEPT(vmcb, VMEXIT_GDTR_READ);
    SET_INTERCEPT(vmcb, VMEXIT_IDTR_READ);
    SET_INTERCEPT(vmcb, VMEXIT_CR4_WRITE);

    vcpu->guest_vmcb->control.msrpm_base_pa = msrpm_pa;

    vcpu->guest_vmcb->control.guest_asid = 1;
    vcpu->guest_vmcb->control.np_enable |= 1;
    vcpu->guest_vmcb->control.n_cr3 = pml4_pa;
    vcpu->guest_vmcb->control.tlb_ctl = 1;

    vcpu->guest_vmcb->save.gdtr.base = gdtr.address;
    vcpu->guest_vmcb->save.gdtr.limit = gdtr.size;

    vcpu->guest_vmcb->save.idtr.base = idtr.address;
    vcpu->guest_vmcb->save.idtr.limit = idtr.size;


    asm volatile (
        "mov %%cs, %0;"
        "mov %%ds, %1;"
        "mov %%es, %2;"
        "mov %%ss, %3;"

        : "=r"(cs), "=r"(ds), "=r"(es), "=r"(ss)
    );

    native_store_gdt(&gdtr);
    gdt = (struct desc_struct *)gdtr.address;

    // vmsave

    vcpu->guest_vmcb->save.cpl = 0;

    ac_copy_segment(gdt, cs, &vcpu->guest_vmcb->save.cs);
    ac_copy_segment(gdt, ds, &vcpu->guest_vmcb->save.ds);
    ac_copy_segment(gdt, es, &vcpu->guest_vmcb->save.es);
    ac_copy_segment(gdt, ss, &vcpu->guest_vmcb->save.ss);

    // MSRs
    rdmsrq(MSR_EFER, tmpmsr);
    vcpu->guest_vmcb->save.efer = tmpmsr;

    rdmsrq(MSR_IA32_CR_PAT, tmpmsr);
    vcpu->guest_vmcb->save.g_pat = tmpmsr;

    ac_vmsave(vcpu->guest_vmcb);

    rdmsrq(MSR_KERNEL_GS_BASE, tmpmsr);
    vcpu->guest_vmcb->save.kernel_gs_base = tmpmsr;

    rdmsrq(MSR_FS_BASE, tmpmsr);
    vcpu->guest_vmcb->save.fs.base = tmpmsr;

    rdmsrq(MSR_GS_BASE, tmpmsr);
    vcpu->guest_vmcb->save.gs.base = tmpmsr;

    rdmsrq(MSR_STAR, tmpmsr);
    vcpu->guest_vmcb->save.star = tmpmsr;
    rdmsrq(MSR_LSTAR, tmpmsr);
    vcpu->guest_vmcb->save.lstar = tmpmsr;
    rdmsrq(MSR_CSTAR, tmpmsr);
    vcpu->guest_vmcb->save.cstar = tmpmsr;
    rdmsrq(MSR_SYSCALL_MASK, tmpmsr);
    vcpu->guest_vmcb->save.sfmask = tmpmsr;

    // CRs
    vcpu->guest_vmcb->save.cr0 = native_read_cr0();
    vcpu->guest_vmcb->save.cr2 = native_read_cr2();
    vcpu->guest_vmcb->save.cr3 = __read_cr3();
    vcpu->guest_vmcb->save.cr4 = native_read_cr4() & ~X86_CR4_UMIP;


    // SRs
    vcpu->guest_vmcb->save.rflags = native_save_fl(); // we don't care since the asm stub just returns
    vcpu->guest_vmcb->save.rsp = U64_MAX; // will be filled by ac_hv_launch
    vcpu->guest_vmcb->save.rip = (u64)ac_hv_resume_guest;
    vcpu->guest_vmcb->control.int_shadow = 0;

    vcpu->stack_layout.top.self = vcpu;
    vcpu->stack_layout.top.host_vmcb_pa = __pa(vcpu->host_vmcb);
    vcpu->stack_layout.top.guest_vmcb_pa = __pa(vcpu->guest_vmcb);
    vcpu->stack_layout.top.vmcb_rsp_ptr = &vcpu->guest_vmcb->save.rsp;
    vcpu->stack_layout.top.guest_vmcb = vcpu->guest_vmcb;

    rdmsrq(MSR_VM_HSAVE_PA, tmpmsr);
    vcpu->shadow_hsave_pa = tmpmsr;

    wrmsrq(MSR_VM_HSAVE_PA, __pa(vcpu->host_state_area));

    ac_vmsave(vcpu->host_vmcb);
    if (!root_pgd) panic("ac_hijack_core(%d): called but init_mm is not initialized.\n", cpu);

    if (ac_hv_launch(&vcpu->stack_layout.top.guest_vmcb_pa, __pa(root_pgd->pgd)) == 0) {
        pr_info("core %d: recieved control back in guest mode.\n", cpu);
    } else {
        pr_err("core %d: invalid return code.\n", cpu);
    }

    set_cpu_cap(&boot_cpu_data, X86_FEATURE_UMIP);
}

static int ac_hv_pm_notifier(struct notifier_block *nb, unsigned long action, void *data) {
    switch (action) {
        case PM_SUSPEND_PREPARE:
        case PM_HIBERNATION_PREPARE:
            pr_info("system suspending. devirtualizing all cores.\n");
            on_each_cpu(ac_devirtualize_core, NULL, 1);
            break;

        case PM_POST_SUSPEND:
        case PM_POST_HIBERNATION:
        case PM_POST_RESTORE:
            pr_info("system waking up. re-virtualizing all cores.\n");
            on_each_cpu(ac_hijack_core, NULL, 1);
            break;
    }
    return NOTIFY_OK;
}

static bool ac_fallback_dac_check(struct task_struct *task) {
    const struct cred *my_cred = current_cred();
    const struct cred *target_cred;
    bool allowed = false;

    if (capable(CAP_SYS_PTRACE))
        return true;

    target_cred = get_task_cred(task);
    if (!target_cred)
        return false;

    if (uid_eq(my_cred->uid, target_cred->uid) &&
        uid_eq(my_cred->uid, target_cred->euid) &&
        uid_eq(my_cred->uid, target_cred->suid) &&
        gid_eq(my_cred->gid, target_cred->gid) &&
        gid_eq(my_cred->gid, target_cred->egid) &&
        gid_eq(my_cred->gid, target_cred->sgid)) {
        allowed = true;
    }

    put_cred(target_cred);
    return allowed;
}

static bool ac_has_ptrace_permission(struct task_struct *task) {
    // TODO: this is bad, but i don't know how to fix it right now

    if (likely(real_ptrace_may_access)) {
        return real_ptrace_may_access(task, PTRACE_MODE_ATTACH_REALCREDS);
    }

    pr_warn_once("alcor: ptrace_may_access unresolved, falling back to manual DAC check\n");
    return ac_fallback_dac_check(task);
}

static int ac_fix_pid(struct ac_hook_data *data) {
    struct task_struct *task;
    struct pid *pid;

    if (data->pid == 0) {
        data->pid = current->tgid;
    } else {
        pid = find_get_pid(data->pid);
        if (!pid) {
            return -ESRCH;
        }

        task = get_pid_task(pid, PIDTYPE_PID);
        if (!task) {
            put_pid(pid);
            return -ESRCH;
        }

        if (!ac_has_ptrace_permission(task->group_leader)) {
            put_task_struct(task);
            put_pid(pid);
            return -EPERM;
        }

        data->pid = task->tgid;
        put_task_struct(task);
        put_pid(pid);
    }

    return 0;
}

static long ac_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    pid_t target_pid;
    int rc;
    struct ac_hook_data *data_ptr, *old_data, data;

    switch (cmd) {
        case AC_IOCTL_UNHOOK:
            if (copy_from_user(&target_pid, (pid_t __user *)arg, sizeof(pid_t))) {
                return -EFAULT;
            }

            if ((rc = ac_fix_pid(&data)) != 0) {
                return rc;
            }

            old_data = xa_erase(&hooklist, target_pid);
            if (!old_data) return -ENOENT;

            kfree(old_data);

            return 0;
        case AC_IOCTL_HOOK:
            if (copy_from_user(&data, (struct ac_hook_data __user *)arg, sizeof(data))) {
                return -EFAULT;
            }

            if ((rc = ac_fix_pid(&data)) != 0) {
                return rc;
            }

            data_ptr = kmalloc(sizeof(*data_ptr), GFP_KERNEL);
            if (!data_ptr) return -ENOMEM;

            *data_ptr = data;

            old_data = xa_store(&hooklist, data.pid, data_ptr, GFP_KERNEL);
            if (xa_is_err(old_data)) {
                kfree(data_ptr);
                return xa_err(old_data);
            } else if (old_data) {
                kfree(old_data);
            }

            return 0;
        default:
            pr_info("unsupported ioctl command: %u\n", cmd);
            return -ENOTTY;
    }
}

static const struct file_operations ac_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = ac_ioctl,
};

static struct miscdevice ac_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "alcor_ctrl",
    .fops  = &ac_fops,
    .mode  = 0666,
};

static struct notifier_block ac_pm_nb = {
    .notifier_call = ac_hv_pm_notifier,
};

static int __init ac_init(void) {
    int cpu, errno = -ENOMEM;
    u64 cr4;
    struct ac_vcpu_ctx *vcpu;

    if (!ac_is_svm_supported()) {
        return -ENODEV;
    }

    root_pgd = ac_find_ksym("init_mm");

    cr4 = native_read_cr4();
    if (cr4 & X86_CR4_UMIP) {
        pr_info("umip is enabled. it will be disabled when entering guest mode.\n");
        used_to_have_umip = true;
    }

    mapping_tables = ac_npt_build_mapping_tables();
    if (!mapping_tables) {
        pr_err("failed to build mapping tables.\n");
        return -ENOMEM;
    }

    ac_msrpm_set_intercept(mapping_tables->msrpm, MSR_EFER, MSRPM_INTERCEPT_WRITE);
    ac_msrpm_set_intercept(mapping_tables->msrpm, MSR_EFER, MSRPM_INTERCEPT_READ);
    ac_msrpm_set_intercept(mapping_tables->msrpm, MSR_VM_HSAVE_PA, MSRPM_INTERCEPT_WRITE);
    ac_msrpm_set_intercept(mapping_tables->msrpm, MSR_VM_HSAVE_PA, MSRPM_INTERCEPT_READ);


    if (misc_register(&ac_device)) {
        pr_err("failed to register misc device.\n");
        errno = -ENODEV;
        goto error;
    }

    cpus_read_lock();

    for_each_online_cpu(cpu) {
        vcpu = ac_allocate_vcpu_ctx(mapping_tables);
        if (!vcpu) goto error;
        per_cpu(host_vcpu, cpu) = vcpu;
    }
    pr_info("mapping tables are allocated, used %d pte pages.\n", mapping_tables->pte_pool_used);

    if (register_pm_notifier(&ac_pm_nb) < 0) {
        pr_err("failed to pm notifier.\n");
        errno = -ENODEV;
        goto error_alloc;
    }

    on_each_cpu(ac_hijack_core, NULL, 1);
    pr_info("all cores are back in guest mode.\n");

    cpus_read_unlock();

    return 0;

error_alloc:
    cpus_read_unlock();
    misc_deregister(&ac_device);
error:
    for_each_online_cpu(cpu) {
        ac_free_vcpu_ctx(per_cpu(host_vcpu, cpu));
    }
    xa_destroy(&hooklist);
    ac_npt_free_mapping_tables(mapping_tables);
    return errno;
}

static void __exit ac_exit(void) {
    int cpu;

    pr_info("devirtualizing all cores.\n");
    on_each_cpu(ac_devirtualize_core, NULL, 1);
    unregister_pm_notifier(&ac_pm_nb);
    misc_deregister(&ac_device);

    for_each_online_cpu(cpu) {
        ac_free_vcpu_ctx(per_cpu(host_vcpu, cpu));
    }
    ac_npt_free_mapping_tables(mapping_tables);
    xa_destroy(&hooklist);

    pr_info("unloaded.\n");
}

module_init(ac_init);
module_exit(ac_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A hypervisor for emulating the results of cpuid and sgdt/sidt");
