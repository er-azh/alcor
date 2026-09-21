# Alcor
Alcor is a [Blue Pill](https://en.wikipedia.org/wiki/Blue_Pill_(software))-style SVM hypervisor for Linux, but unlike Blue Pill, it's a compatibility tool: it emulates the results of `cpuid` and `gdtr`/`idtr` reads for processors that don't natively support UMIP or CPUID faulting.

After loading, the module exposes a misc device at `/dev/alcor_ctrl`, which allows every process to change the `cpuid` and descriptor view of itself or, with `CAP_SYS_PTRACE`, any other process. See [Usage](#usage) for the exposed API.

# Stability
This is extremely experimental, but it is completely stable on everything I could test it on; however, I still strongly suggest saving your existing work before attempting to load this for the first time.

This is also the first time I've written a kernel module, so security and stability are not guaranteed.

While I really wanted to list the exact tested hardware in this section, unfortunately I can't offer anything more than "It works on my machine" at the moment. If you encounter a crash, please create an issue with the oops message or anything else you think could be useful to help diagnose it.

# Building and Loading
Ensure that you have an AMD machine with SVM support, and that SVM is enabled in the BIOS/UEFI.
```sh
grep -m1 -o 'svm' /proc/cpuinfo # should print `svm`
```
Then make sure the kernel headers and GCC are installed. Recent kernel versions are supported (tested on 6.12-7.2). If you encounter a compile error, please open an issue.

For example, under Arch Linux (assuming you're using `linux` for the kernel):
```sh
pacman -S base-devel linux-headers
```
Ensure lockdown/Secure Boot config allows loading modules, then run:
```sh
make load
```
Note for `doas` users: `make load` ends up running `sudo insmod alcor.ko`, which requires sudo. Use a shim, edit it out, or just run `insmod` manually. The same also applies to `make unload`.

You can then check `dmesg` to see if all cores are virtualized properly.

# Unloading
The module is expected to handle `rmmod` normally. Every core is devirtualized upon unloading, and the kernel resumes in host mode.

To unload using the provided Makefile, simply do:
```sh
make unload
```

# Usage
See [api.h](api.h) for the full exposed API. Here's an example:
```c
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include "api.h"

struct ac_hook_data hook = { 0 };

// spoof the CPUID brand string (leaves 0x80000002-0x80000004)
const char *brand = "Navi (R) 2nd model 69420 @ 69420GHz             ";
for (int i = 0; i < 3; i++) {
    // You can add up to 16 overrides
    struct ac_cpuid_entry *e = &hook.cpuid_overrides[hook.cpuid_count++]; 
    e->flags = AC_CPUID_PRESET_MASK;
    e->leaf  = 0x80000002 + i;
    memcpy(e->regs, brand + i * 16, 16);
}

// spoof sgdt/sidt results
hook.segment_flags = AC_GDTR_HOOK_MASK | AC_IDTR_HOOK_MASK;
hook.segments_overrides[AC_GDTR_HOOK] = (struct ac_desc_ptr){ .address = 0x6969, .size = 0x420 };
hook.segments_overrides[AC_IDTR_HOOK] = (struct ac_desc_ptr){ .address = 0x420, .size = 0x6969 };

hook.log_cpuid = true; // log every intercepted cpuid to dmesg

int fd = open("/dev/alcor_ctrl", O_RDWR);
hook.pid = 0; // 0 = calling process, or any pid if you have CAP_SYS_PTRACE
ioctl(fd, AC_IOCTL_HOOK, &hook); // takes effect immediately

int pid = 0;
// remove the hooks again: UNHOOK only takes a pid
ioctl(fd, AC_IOCTL_UNHOOK, &pid);
```

# Limitations & TODOs
- Entries are not cleared upon process exit. This is the most important TODO. At the moment, users are expected to clean their hooks on exit.
- Only `gdtr` and `idtr` reads are currently supported.
- Nested page faults are treated as fatal; however, they should not happen unless the NPT is corrupted.
- You cannot run KVM guests while the module is loaded (merely having the KVM module loaded is fine). Running this module *inside* a KVM guest is supported. See [FAQ](#faq).
- AMD-only.

# What This Is Not

- **Not a rootkit.** Nothing hides itself: the module shows up in `lsmod`, and `rmmod` should cleanly devirtualize.
- **Not a security boundary.** The kernel and all CPL0 code see the real machine. This module only cares about what *unprivileged* code sees, nothing more.
- **Not invisible.** The default GDT base is a constant, `cpuid` takes a vmexit (measurable latency), and the hypervisor bit is set on leaf 1. A determined program can detect it. The goal is to emulate `cpuid` and descriptor reads. Not stealth.

# FAQ
**Is this a rootkit?**
No. Nothing is hidden from the kernel. Only unprivileged code is affected by the emulation. See [What This Is Not](#what-this-is-not).

**Can software detect it?**
Yes. Local software can easily check for the module or the existence of `/dev/alcor_ctrl`.

**Does it work under KVM?**
Yes, but with strong emphasis on the word *under*. You can use this in KVM guests with nested SVM enabled. There's also support for forwarding CPL0 hypercalls to upstream KVM; however, **you cannot run KVM guests while this module is loaded**.

**Why does it disable UMIP?**
Because, while loaded, it replaces UMIP with a softer version that returns a constant value for every read by default. This also lets the hypervisor control the emulated values on a per-process basis when an override is active.

**Is this vibe-coded?** No. LLMs were involved in the initial research phase; however most of the code was written by hand over the course of a week.

# License
GPL-2.0 or later
