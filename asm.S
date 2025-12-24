#include <linux/linkage.h>
#include <asm/nospec-branch.h>
#include <asm/msr-index.h>
#include "hypercalls.h"

STACK_FRAME_NON_STANDARD(ac_hv_launch);
STACK_FRAME_NON_STANDARD(ac_hv_resume_guest);

.macro putc char
    pushfq
    push %rax
    push %rdx
    mov $0x3f8, %dx
    mov $\char, %al
    out %al, %dx
    pop %rdx
    pop %rax
    popfq
.endm

.macro pushgprs
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rdi
    push %rsi
    push %rbp
    push %rbx
    push %rdx
    push %rcx
    push %rax
.endm

.macro popgprs
    pop %rax
    pop %rcx
    pop %rdx
    pop %rbx
    pop %rbp
    pop %rsi
    pop %rdi
    pop %r8
    pop %r9
    pop %r10
    pop %r11
    pop %r12
    pop %r13
    pop %r14
    pop %r15
.endm

.macro pushsv
    push %rbx                // push sysv callee saved regs
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
.endm

.macro popsv
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
.endm

// args:
// RDI (arg1) = vstack base
SYM_FUNC_START(ac_hv_launch)
    pushsv                   // save sysv callee saved regs

    xchg %rsp, %rdi          // pivot to hv stack

    mov 24(%rsp), %rax       // vmcb_rsp_ptr
    mov %rdi, (%rax)         // *vmcb_rsp_ptr = old_rsp

    mov %rsi, %cr3           // pivot to root cr3
    mov (%rsp), %rax         // load guest's vmcb pa

.Lvmloop:
    vmload %rax              // vmload guest state
    vmrun %rax               // rax and rsp are ours, the rest is guest's, GIF=0
    vmsave %rax              // load FS, GS, TR, LDTR, KernelGsBase, STAR, LSTAR, CSTAR, SFMASK
                             // SYSENTER_CS, SYSENTER_ESP, SYSENTER_EIP
    pushgprs

    add $0x1000, %rax        // rax = guest_vmcb + 1
    vmload %rax              // vmload host_vmcb
    sub $0x1000, %rax        // rax = guest_vmcb

    mov %rsp, %rdi           // arg1 = gprs
    call ac_hv_vmexit        // ac_hv_vmexit(gprs), gprs->rax is vmcb_pa
    test %eax, %eax

    popgprs
    jz .Lvmloop              // loop if not exiting

    vmload %rax              // load guest state

    push %rax                // write EFER
    push %rdx
    push %rcx
    mov $MSR_EFER, %rcx
    mov %r10, %rax
    mov %r10, %rdx
    shr $32, %rdx
    wrmsr
    pop %rcx
    pop %rdx
    pop %rax

    mov %r11, %cr4           // write CRs
    mov %r12, %cr3
    mov %r13, %cr0

    push %rdi
    popfq

    xchg %rsp, %r15          // pivot to guest stack
    JMP_NOSPEC r14
SYM_FUNC_END(ac_hv_launch)

SYM_FUNC_START(ac_hv_resume_guest)
    ENDBR64
    mov %cr4, %rdi
    and $~(1<<11), %rdi
    mov %rdi, %cr4
    popsv                    // pop sysv callee saved regs
    xor %eax, %eax           // ret = 0
    RET
SYM_FUNC_END(ac_hv_resume_guest)

SYM_FUNC_START(ac_hv_devirtualize)
    pushsv
    mov $DEVIRTUALIZE_MAGIC, %rdi
    vmmcall
    popsv
    xor %eax, %eax
    wbinvd
    stgi
    RET
SYM_FUNC_END(ac_hv_devirtualize)
