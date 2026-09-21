#ifndef _AC_API_H
#define _AC_API_H
#include <linux/types.h>

#ifndef __KERNEL__
#include <stdbool.h>
#endif

#define AC_MAX_CPUID_OVERRIDES 16

enum ac_segment_hook {
    AC_GDTR_HOOK = 0,
    AC_IDTR_HOOK,
    AC_LDTR_HOOK,
    AC_TR_HOOK,
    AC_SEGMENT_LAST,
};

#define AC_GDTR_HOOK_MASK   (1 << AC_GDTR_HOOK)
#define AC_IDTR_HOOK_MASK   (1 << AC_IDTR_HOOK)
#define AC_LDTR_HOOK_MASK   (1 << AC_LDTR_HOOK)
#define AC_TR_HOOK_MASK     (1 << AC_TR_HOOK)

enum ac_cpuid_register {
    AC_CPUID_EAX = 0,
    AC_CPUID_EBX,
    AC_CPUID_ECX,
    AC_CPUID_EDX,
    AC_CPUID_REGISTER_LAST,
};

#define AC_CPUID_EAX_MASK            (1 << AC_CPUID_EAX)
#define AC_CPUID_EBX_MASK            (1 << AC_CPUID_EBX)
#define AC_CPUID_ECX_MASK            (1 << AC_CPUID_ECX)
#define AC_CPUID_EDX_MASK            (1 << AC_CPUID_EDX)
#define AC_CPUID_HAS_SUBLEAF_MASK    (1 << AC_CPUID_REGISTER_LAST)
#define AC_CPUID_PRESET_MASK         (AC_CPUID_EAX_MASK | AC_CPUID_EBX_MASK | AC_CPUID_ECX_MASK | AC_CPUID_EDX_MASK)

struct ac_desc_ptr {
	unsigned short size;
	unsigned long address;
} __attribute__((packed)) ;

struct ac_cpuid_entry {
    __u32 leaf;
    __u32 subleaf;
    __u8 flags;
    __u32 regs[AC_CPUID_REGISTER_LAST];
};

struct ac_hook_data {
    pid_t pid;
    bool log_cpuid;
    __u32 cpuid_count;
    __u32 segment_flags;
    struct ac_cpuid_entry cpuid_overrides[AC_MAX_CPUID_OVERRIDES];
    struct ac_desc_ptr segments_overrides[AC_SEGMENT_LAST];
};

#define AC_IOCTL_MAGIC 'z'
#define AC_IOCTL_HOOK _IOW(AC_IOCTL_MAGIC, 0, struct ac_hook_data)
#define AC_IOCTL_UNHOOK _IOW(AC_IOCTL_MAGIC, 1, int)


#endif /* _AC_API_H */
