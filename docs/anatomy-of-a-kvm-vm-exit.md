# Anatomy of a KVM VM Exit

## Purpose

This document follows one guest from `KVM_RUN` into guest execution, through multiple VM exits, and finally back to the userspace VMM.

The main example is the Week 2 CPUID workload because it demonstrates all of the important layers in a very small program:

- guest entry
- a nested page fault handled inside KVM
- a CPUID exit handled inside KVM
- guest re-entry
- a final HLT exit returned to userspace

The key distinction is between a VM exit into KVM and a return from `KVM_RUN` into the VMM.

---

## The Userspace Side

The VMM creates a VM and one vCPU, maps `struct kvm_run`, initializes registers and guest memory, and finally enters the execution loop with:

```text
ioctl(vcpu_fd, KVM_RUN, 0)
```

At that point the VMM thread enters the kernel. KVM prepares the vCPU and eventually transfers execution to the guest.

The userspace loop only regains control if KVM decides that the event must be handled in userspace or that guest execution cannot continue normally.

When `KVM_RUN` returns, the VMM reads:

```text
kvm_run->exit_reason
```

and handles values such as `KVM_EXIT_IO` and `KVM_EXIT_HLT`.

---

## The CPUID Workload

The guest is:

```asm
mov eax, 0
cpuid
hlt
```

The raw layout is:

```text
RIP 0x0: 66 b8 00 00 00 00    mov eax, 0
RIP 0x6: 0f a2                cpuid
RIP 0x8: f4                   hlt
```

Before entering the guest, the VMM obtains KVM's supported CPUID model with `KVM_GET_SUPPORTED_CPUID` and applies that table to the vCPU using `KVM_SET_CPUID2`.

On this host, `KVM_GET_SUPPORTED_CPUID` returned 67 entries. The first entry, CPUID leaf 0, reported the vendor string `AuthenticAMD` and a maximum basic leaf of `0x10`.

---

## Observed Trace

The relevant trace was:

```text
kvm_entry                 vcpu 0 rip 0x0
kvm_exit                  vcpu 0 reason npf rip 0x0
kvm_page_fault            vcpu 0 rip 0x0

kvm_entry                 vcpu 0 rip 0x0
kvm_exit                  vcpu 0 reason cpuid rip 0x6
kvm_cpuid                 func 0 idx 0 ...

kvm_entry                 vcpu 0 rip 0x8
kvm_exit                  vcpu 0 reason idle-halt rip 0x8
kvm_userspace_exit        reason KVM_EXIT_HLT (5)
```

The VMM itself reported:

```text
workload=cpuid iterations=1 ... userspace_exits=1 io_exits=0 hlt_exits=1
```

So the trace shows three VM exits, while the VMM sees only one return from `KVM_RUN`.

---

## Step 1: Userspace Calls `KVM_RUN`

The VMM calls:

```text
ioctl(vcpu_fd, KVM_RUN, 0)
```

Control moves from the userspace VMM into the KVM kernel code.

KVM prepares the vCPU state and enters the guest.

Trace evidence:

```text
kvm_entry rip=0x0
```

The first guest instruction is at RIP `0x0`.

---

## Step 2: Initial Nested Page Fault

Immediately after the first entry, the trace shows:

```text
kvm_exit reason=npf rip=0x0
kvm_page_fault rip=0x0
```

The processor attempted to access the guest's initial code page and the nested translation was not yet ready for that access. The processor therefore exited guest execution and returned control to KVM.

At this point there is no `kvm_userspace_exit`.

That means KVM handled the event itself. After resolving the translation, KVM re-entered the guest:

```text
kvm_entry rip=0x0
```

The userspace VMM still has not regained control. Its original `KVM_RUN` ioctl is still in progress.

Conceptually:

```text
VMM
 |
 | KVM_RUN
 v
KVM
 |
 | guest entry
 v
Guest @ RIP 0x0
 |
 | NPF
 v
KVM
 |
 | handle page fault internally
 | re-enter guest
 v
Guest @ RIP 0x0
```

---

## Step 3: Guest Executes CPUID

The guest executes `mov eax, 0` and reaches `CPUID` at RIP `0x6`.

The trace shows:

```text
kvm_exit reason=cpuid rip=0x6
kvm_cpuid func=0 idx=0 ...
```

This is a real VM exit from guest execution into KVM.

However, there is still no `kvm_userspace_exit`.

KVM handles the CPUID operation using the vCPU CPUID model previously installed with `KVM_SET_CPUID2`. It produces the guest-visible register results and advances execution past the CPUID instruction.

The next event is:

```text
kvm_entry rip=0x8
```

That is direct evidence that KVM handled CPUID internally and resumed the guest without returning from `KVM_RUN`.

Conceptually:

```text
Guest @ RIP 0x6
 |
 | CPUID
 v
VM exit
 |
 v
KVM
 |
 | emulate/provide CPUID result
 | update guest registers
 | advance RIP
 | re-enter guest
 v
Guest @ RIP 0x8
```

This is why the userspace VMM does not receive a `KVM_EXIT_CPUID` event.

---

## Step 4: Guest Executes HLT

At RIP `0x8`, the guest executes `HLT`.

The trace shows:

```text
kvm_exit reason=idle-halt rip=0x8
kvm_userspace_exit reason=KVM_EXIT_HLT (5)
```

This time KVM does not simply handle the event and re-enter the guest. It reports the halt to userspace.

The original `KVM_RUN` ioctl finally returns.

The VMM reads:

```text
kvm_run->exit_reason == KVM_EXIT_HLT
```

increments its HLT and userspace-exit counters, and stops the run loop.

Conceptually:

```text
Guest @ RIP 0x8
 |
 | HLT
 v
VM exit
 |
 v
KVM
 |
 | decide userspace must observe this exit
 v
kvm_userspace_exit: KVM_EXIT_HLT
 |
 v
KVM_RUN returns
 |
 v
VMM
```

---

## Complete CPUID Control Flow

The whole run is:

```text
userspace VMM
    |
    | ioctl(KVM_RUN)
    v
KVM
    |
    v
kvm_entry #1, RIP 0x0
    |
    | nested page fault
    v
kvm_exit #1: NPF
    |
    | handled inside KVM
    v
kvm_entry #2, RIP 0x0
    |
    | CPUID at RIP 0x6
    v
kvm_exit #2: CPUID
    |
    | kvm_cpuid
    | handled inside KVM
    v
kvm_entry #3, RIP 0x8
    |
    | HLT
    v
kvm_exit #3: idle-halt
    |
    v
kvm_userspace_exit: KVM_EXIT_HLT
    |
    v
KVM_RUN returns
    |
    v
userspace VMM
```

Measured summary:

```text
KVM entries       = 3
KVM exits         = 3
CPUID events      = 1
page-fault events = 1
userspace exits   = 1
```

---

## Contrast With PIO

The PIO workload behaves differently.

For one `OUT 0xe9, AL`, the trace shows:

```text
kvm_entry
kvm_exit                  reason=io
kvm_pio                   pio_write at 0xe9 size 1 count 1
kvm_userspace_exit        reason=KVM_EXIT_IO (2)
```

In this case KVM needs userspace participation, so `KVM_RUN` returns to the VMM for every PIO operation.

The 10,000-iteration PIO workload produced:

```text
VMM counters:
    io_exits        = 10000
    userspace_exits = 10001
    hlt_exits       = 1

Trace counts:
    kvm_pio             = 10000
    kvm_userspace_exit  = 10001
```

The extra userspace exit is the final `HLT`.

A `kvm_stat` snapshot independently reported:

```text
kvm_entry             10010
kvm_exit              10010
kvm_pio               10000
kvm_userspace_exit    10001
```

This reinforces the same distinction: KVM-level exits can be more numerous than userspace returns.

---

## Why This Matters for Performance

A userspace exit is usually more expensive than an exit that KVM can handle entirely in the kernel because control must cross an additional boundary:

```text
Guest
 -> VM exit
 -> KVM
 -> userspace VMM
 -> KVM_RUN again
 -> KVM
 -> guest re-entry
```

The `loop` workload demonstrates the opposite case. It performs 10,000 guest iterations but only one userspace return at the final `HLT`.

The `pio` workload also performs 10,000 iterations, but produces 10,000 `KVM_EXIT_IO` returns plus the final `KVM_EXIT_HLT`.

Therefore guest instruction count or loop count alone is not enough to predict VMM overhead. The important question is which events force transitions across the guest/KVM/userspace boundaries.

---

## Measurement Method

### Aggregate counts

`perf stat` was used with KVM tracepoints to count events such as:

```text
kvm:kvm_entry
kvm:kvm_exit
kvm:kvm_cpuid
kvm:kvm_page_fault
kvm:kvm_pio
kvm:kvm_userspace_exit
```

### Ordered trace

`perf record` captured the tracepoints and `perf script` converted `perf.data` into an ordered event stream. The ordered trace was necessary to connect VM exits to the RIP at which they happened and to observe whether KVM re-entered the guest or returned to userspace.

### KVM-specific snapshot

`kvm_stat` was used in tracepoint mode as an additional aggregate measurement source.

---

## Limitations

The tracepoint timestamps in these experiments are used to understand ordering and control flow, not as precise VM-exit latency measurements. Tracing itself adds overhead.

The one-second `kvm_stat` snapshot is an aggregate observation window. Counts such as the additional `kvm_entry`/`kvm_exit` events are not individually attributed unless a corresponding ordered trace is captured.

The VMM's `runtime_ns` measures the region around the `KVM_RUN` loop. It is not a direct measurement of raw hardware VM-exit latency.

---

## Final Mental Model

The important path is not simply:

```text
VM exit -> userspace
```

It is:

```text
Guest event
    |
    v
VM exit into KVM
    |
    +--> KVM handles it internally
    |        |
    |        v
    |     guest re-entry
    |
    +--> KVM requires userspace
             |
             v
       kvm_userspace_exit
             |
             v
       KVM_RUN returns
             |
             v
       userspace VMM
```

Observed examples in this project:

```text
NPF   -> handled internally by KVM
CPUID -> handled internally by KVM
PIO   -> returned as KVM_EXIT_IO
HLT   -> returned as KVM_EXIT_HLT
```

That distinction is the core of the Week 2 VM-exit model.
