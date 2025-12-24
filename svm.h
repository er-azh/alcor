#ifndef _AC_SVM_H
#define _AC_SVM_H

#include <linux/kernel.h>
#include <asm/page_types.h>

#define VMEXIT_CR0_READ             0x0000
#define VMEXIT_CR1_READ             0x0001
#define VMEXIT_CR2_READ             0x0002
#define VMEXIT_CR3_READ             0x0003
#define VMEXIT_CR4_READ             0x0004
#define VMEXIT_CR5_READ             0x0005
#define VMEXIT_CR6_READ             0x0006
#define VMEXIT_CR7_READ             0x0007
#define VMEXIT_CR8_READ             0x0008
#define VMEXIT_CR9_READ             0x0009
#define VMEXIT_CR10_READ            0x000a
#define VMEXIT_CR11_READ            0x000b
#define VMEXIT_CR12_READ            0x000c
#define VMEXIT_CR13_READ            0x000d
#define VMEXIT_CR14_READ            0x000e
#define VMEXIT_CR15_READ            0x000f
#define VMEXIT_CR0_WRITE            0x0010
#define VMEXIT_CR1_WRITE            0x0011
#define VMEXIT_CR2_WRITE            0x0012
#define VMEXIT_CR3_WRITE            0x0013
#define VMEXIT_CR4_WRITE            0x0014
#define VMEXIT_CR5_WRITE            0x0015
#define VMEXIT_CR6_WRITE            0x0016
#define VMEXIT_CR7_WRITE            0x0017
#define VMEXIT_CR8_WRITE            0x0018
#define VMEXIT_CR9_WRITE            0x0019
#define VMEXIT_CR10_WRITE           0x001a
#define VMEXIT_CR11_WRITE           0x001b
#define VMEXIT_CR12_WRITE           0x001c
#define VMEXIT_CR13_WRITE           0x001d
#define VMEXIT_CR14_WRITE           0x001e
#define VMEXIT_CR15_WRITE           0x001f
#define VMEXIT_DR0_READ             0x0020
#define VMEXIT_DR1_READ             0x0021
#define VMEXIT_DR2_READ             0x0022
#define VMEXIT_DR3_READ             0x0023
#define VMEXIT_DR4_READ             0x0024
#define VMEXIT_DR5_READ             0x0025
#define VMEXIT_DR6_READ             0x0026
#define VMEXIT_DR7_READ             0x0027
#define VMEXIT_DR8_READ             0x0028
#define VMEXIT_DR9_READ             0x0029
#define VMEXIT_DR10_READ            0x002a
#define VMEXIT_DR11_READ            0x002b
#define VMEXIT_DR12_READ            0x002c
#define VMEXIT_DR13_READ            0x002d
#define VMEXIT_DR14_READ            0x002e
#define VMEXIT_DR15_READ            0x002f
#define VMEXIT_DR0_WRITE            0x0030
#define VMEXIT_DR1_WRITE            0x0031
#define VMEXIT_DR2_WRITE            0x0032
#define VMEXIT_DR3_WRITE            0x0033
#define VMEXIT_DR4_WRITE            0x0034
#define VMEXIT_DR5_WRITE            0x0035
#define VMEXIT_DR6_WRITE            0x0036
#define VMEXIT_DR7_WRITE            0x0037
#define VMEXIT_DR8_WRITE            0x0038
#define VMEXIT_DR9_WRITE            0x0039
#define VMEXIT_DR10_WRITE           0x003a
#define VMEXIT_DR11_WRITE           0x003b
#define VMEXIT_DR12_WRITE           0x003c
#define VMEXIT_DR13_WRITE           0x003d
#define VMEXIT_DR14_WRITE           0x003e
#define VMEXIT_DR15_WRITE           0x003f
#define VMEXIT_EXCEPTION_DE         0x0040
#define VMEXIT_EXCEPTION_DB         0x0041
#define VMEXIT_EXCEPTION_NMI        0x0042
#define VMEXIT_EXCEPTION_BP         0x0043
#define VMEXIT_EXCEPTION_OF         0x0044
#define VMEXIT_EXCEPTION_BR         0x0045
#define VMEXIT_EXCEPTION_UD         0x0046
#define VMEXIT_EXCEPTION_NM         0x0047
#define VMEXIT_EXCEPTION_DF         0x0048
#define VMEXIT_EXCEPTION_09         0x0049
#define VMEXIT_EXCEPTION_TS         0x004a
#define VMEXIT_EXCEPTION_NP         0x004b
#define VMEXIT_EXCEPTION_SS         0x004c
#define VMEXIT_EXCEPTION_GP         0x004d
#define VMEXIT_EXCEPTION_PF         0x004e
#define VMEXIT_EXCEPTION_15         0x004f
#define VMEXIT_EXCEPTION_MF         0x0050
#define VMEXIT_EXCEPTION_AC         0x0051
#define VMEXIT_EXCEPTION_MC         0x0052
#define VMEXIT_EXCEPTION_XF         0x0053
#define VMEXIT_EXCEPTION_20         0x0054
#define VMEXIT_EXCEPTION_21         0x0055
#define VMEXIT_EXCEPTION_22         0x0056
#define VMEXIT_EXCEPTION_23         0x0057
#define VMEXIT_EXCEPTION_24         0x0058
#define VMEXIT_EXCEPTION_25         0x0059
#define VMEXIT_EXCEPTION_26         0x005a
#define VMEXIT_EXCEPTION_27         0x005b
#define VMEXIT_EXCEPTION_28         0x005c
#define VMEXIT_EXCEPTION_VC         0x005d
#define VMEXIT_EXCEPTION_SX         0x005e
#define VMEXIT_EXCEPTION_31         0x005f
#define VMEXIT_INTR                 0x0060
#define VMEXIT_NMI                  0x0061
#define VMEXIT_SMI                  0x0062
#define VMEXIT_INIT                 0x0063
#define VMEXIT_VINTR                0x0064
#define VMEXIT_CR0_SEL_WRITE        0x0065
#define VMEXIT_IDTR_READ            0x0066
#define VMEXIT_GDTR_READ            0x0067
#define VMEXIT_LDTR_READ            0x0068
#define VMEXIT_TR_READ              0x0069
#define VMEXIT_IDTR_WRITE           0x006a
#define VMEXIT_GDTR_WRITE           0x006b
#define VMEXIT_LDTR_WRITE           0x006c
#define VMEXIT_TR_WRITE             0x006d
#define VMEXIT_RDTSC                0x006e
#define VMEXIT_RDPMC                0x006f
#define VMEXIT_PUSHF                0x0070
#define VMEXIT_POPF                 0x0071
#define VMEXIT_CPUID                0x0072
#define VMEXIT_RSM                  0x0073
#define VMEXIT_IRET                 0x0074
#define VMEXIT_SWINT                0x0075
#define VMEXIT_INVD                 0x0076
#define VMEXIT_PAUSE                0x0077
#define VMEXIT_HLT                  0x0078
#define VMEXIT_INVLPG               0x0079
#define VMEXIT_INVLPGA              0x007a
#define VMEXIT_IOIO                 0x007b
#define VMEXIT_MSR                  0x007c
#define VMEXIT_TASK_SWITCH          0x007d
#define VMEXIT_FERR_FREEZE          0x007e
#define VMEXIT_SHUTDOWN             0x007f
#define VMEXIT_VMRUN                0x0080
#define VMEXIT_VMMCALL              0x0081
#define VMEXIT_VMLOAD               0x0082
#define VMEXIT_VMSAVE               0x0083
#define VMEXIT_STGI                 0x0084
#define VMEXIT_CLGI                 0x0085
#define VMEXIT_SKINIT               0x0086
#define VMEXIT_RDTSCP               0x0087
#define VMEXIT_ICEBP                0x0088
#define VMEXIT_WBINVD               0x0089
#define VMEXIT_MONITOR              0x008a
#define VMEXIT_MWAIT                0x008b
#define VMEXIT_MWAIT_CONDITIONAL    0x008c
#define VMEXIT_XSETBV               0x008d
#define VMEXIT_EFER_WRITE_TRAP      0x008f
#define VMEXIT_CR0_WRITE_TRAP       0x0090
#define VMEXIT_CR1_WRITE_TRAP       0x0091
#define VMEXIT_CR2_WRITE_TRAP       0x0092
#define VMEXIT_CR3_WRITE_TRAP       0x0093
#define VMEXIT_CR4_WRITE_TRAP       0x0094
#define VMEXIT_CR5_WRITE_TRAP       0x0095
#define VMEXIT_CR6_WRITE_TRAP       0x0096
#define VMEXIT_CR7_WRITE_TRAP       0x0097
#define VMEXIT_CR8_WRITE_TRAP       0x0098
#define VMEXIT_CR9_WRITE_TRAP       0x0099
#define VMEXIT_CR10_WRITE_TRAP      0x009a
#define VMEXIT_CR11_WRITE_TRAP      0x009b
#define VMEXIT_CR12_WRITE_TRAP      0x009c
#define VMEXIT_CR13_WRITE_TRAP      0x009d
#define VMEXIT_CR14_WRITE_TRAP      0x009e
#define VMEXIT_CR15_WRITE_TRAP      0x009f
#define VMEXIT_NPF                  0x0400
#define AVIC_INCOMPLETE_IPI         0x0401
#define AVIC_NOACCEL                0x0402
#define VMEXIT_VMGEXIT              0x0403
#define VMEXIT_INVALID              -1

struct vmcb_control_area {
    u16 intercept_cr_read;              // +0x000
    u16 intercept_cr_write;             // +0x002
    u16 intercept_dr_read;              // +0x004
    u16 intercept_dr_write;             // +0x006
    u32 intercept_exceptions;           // +0x008
    u32 intercept_misc1;                // +0x00c
    u32 intercept_misc2;                // +0x010
    u8 reserved1[0x03c - 0x014];        // +0x014
    u16 pause_filter_thresh;            // +0x03c
    u16 pause_filter_count;             // +0x03e
    u64 iopm_base_pa;                   // +0x040
    u64 msrpm_base_pa;                  // +0x048
    u64 tsc_offset;                     // +0x050
    u32 guest_asid;                     // +0x058
    u32 tlb_ctl;                        // +0x05c
    u64 v_intr;                         // +0x060
    u64 int_shadow;                     // +0x068
    u64 exitcode;                       // +0x070
    u64 exitinfo1;                      // +0x078
    u64 exitinfo2;                      // +0x080
    u64 exit_int_info;                  // +0x088
    u64 np_enable;                      // +0x090
    u64 avic_apic_bar;                  // +0x098
    u64 guest_pa_of_ghcb;               // +0x0a0
    u64 eventinj;                       // +0x0a8
    u64 n_cr3;                          // +0x0b0
    u64 lbr_virtualization_enable;      // +0x0b8
    u64 vmcb_clean;                     // +0x0c0
    u64 nrip;                           // +0x0c8
    u8 num_of_bytes_fetched;            // +0x0d0
    u8 guest_insn_bytes[15];            // +0x0d1
    u64 avic_apic_backing_page_ptr;     // +0x0e0
    u64 reserved2;                      // +0x0e8
    u64 avic_logical_table_ptr;         // +0x0f0
    u64 avic_physical_table_ptr;        // +0x0f8
    u64 reserved3;                      // +0x100
    u64 vmcb_save_state_ptr;            // +0x108
    u8 reserved4[0x400 - 0x110];        // +0x110
};
static_assert(sizeof(struct vmcb_control_area) == 0x400,
              "vmcb_control_area size mismatch");

struct vmcb_segment_attribute
{
    u16 type : 4;  // [0:3]
    u16 s : 1;     // [4]
    u16 dpl : 2;   // [5:6]
    u16 p : 1;     // [7]
    u16 avl : 1;   // [8]
    u16 l : 1;     // [9]
    u16 d : 1;     // [10]
    u16 g : 1;     // [11]
    u16 reserved1 : 4;
};
static_assert(sizeof(struct vmcb_segment_attribute) == sizeof(u16),
              "SEGMENT_ATTRIBUTE Size Mismatch");


struct vmcb_segment {
    u16 selector;
    struct vmcb_segment_attribute attrib;
    u32 limit;
    u64 base;
};
static_assert(sizeof(struct vmcb_segment) == 0x10,
              "vmcb_segment size mismatch");

struct vmcb_save_area {
    struct vmcb_segment es;             // +0x000
    struct vmcb_segment cs;             // +0x010
    struct vmcb_segment ss;             // +0x020
    struct vmcb_segment ds;             // +0x030
    struct vmcb_segment fs;             // +0x040
    struct vmcb_segment gs;             // +0x050
    struct vmcb_segment gdtr;           // +0x060
    struct vmcb_segment ldtr;           // +0x070
    struct vmcb_segment idtr;           // +0x080
    struct vmcb_segment tr;             // +0x090
    u8 reserved1[0x0cb - 0x0a0];        // +0x0a0
    u8 cpl;                             // +0x0cb
    u32 reserved2;                      // +0x0cc
    u64 efer;                           // +0x0d0
    u8 reserved3[0x148 - 0x0d8];        // +0x0d8
    u64 cr4;                            // +0x148
    u64 cr3;                            // +0x150
    u64 cr0;                            // +0x158
    u64 dr7;                            // +0x160
    u64 dr6;                            // +0x168
    u64 rflags;                         // +0x170
    u64 rip;                            // +0x178
    u8 reserved4[0x1d8 - 0x180];        // +0x180
    u64 rsp;                            // +0x1d8
    u8 reserved5[0x1f8 - 0x1e0];        // +0x1e0
    u64 rax;                            // +0x1f8
    u64 star;                           // +0x200
    u64 lstar;                          // +0x208
    u64 cstar;                          // +0x210
    u64 sfmask;                         // +0x218
    u64 kernel_gs_base;                 // +0x220
    u64 sysenter_cs;                    // +0x228
    u64 sysenter_esp;                   // +0x230
    u64 sysenter_eip;                   // +0x238
    u64 cr2;                            // +0x240
    u8 reserved6[0x268 - 0x248];        // +0x248
    u64 g_pat;                          // +0x268
    u64 dbgctl;                         // +0x270
    u64 br_from;                        // +0x278
    u64 br_to;                          // +0x280
    u64 last_excp_from;                 // +0x288
    u64 last_excp_to;                   // +0x290
};
static_assert(sizeof(struct vmcb_save_area) == 0x298,
              "vmcb_save_area size mismatch");

struct vmcb {
    struct vmcb_control_area control;
    struct vmcb_save_area save;
    u8 reserved1[PAGE_SIZE - sizeof(struct vmcb_control_area) - sizeof(struct vmcb_save_area)];
} __attribute__((packed, aligned(PAGE_SIZE)));
static_assert(sizeof(struct vmcb) == PAGE_SIZE,
              "vmcb size mismatch");



static_assert(offsetof(struct vmcb_control_area, eventinj) == 0x0A8);
static_assert(offsetof(struct vmcb_save_area, last_excp_to) == 0x290);

#endif /* _AC_SVM_H */
