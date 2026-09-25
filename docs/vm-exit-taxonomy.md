# VM Exit Taxonomy

## Purpose

This note documents the exit types observed in the Week 2 KVM workloads and, more importantly, separates two different concepts:

1. a VM exit from guest execution back into KVM, and
2. a return from `KVM_RUN` back to the userspace VMM.

They are not the same event.

The experiments in this week used four workloads:

- `hlt`
- `loop`
- `pio`
- `cpuid`

The VMM runs directly against `/dev/kvm`, with one vCPU and a small real-mode guest.

---

## Terminology

### Guest entry

A guest entry is the transition from KVM into guest execution. In tracing this is visible as:

```text
kvm:kvm_entry
```

### VM exit

A VM exit is a transition from guest execution back into KVM because the processor encountered an event that requires virtualization handling.

In tracing this is visible as:

```text
kvm:kvm_exit
```

Examples observed in this project include:

- nested page fault (`npf`)
- `cpuid`
- port I/O (`io`)
- halt (`idle-halt`)

A VM exit does **not** automatically mean that `KVM_RUN` returns to userspace.

### Userspace exit

A userspace exit happens when KVM decides that the userspace VMM must handle the event. This is visible as:

```text
kvm:kvm_userspace_exit
```

and corresponds to `ioctl(vcpu_fd, KVM_RUN, 0)` returning to the VMM, after which the VMM reads `struct kvm_run::exit_reason`.

Observed userspace exit reasons in this project are:

- `KVM_EXIT_IO`
- `KVM_EXIT_HLT`

---

## Observed Exit Classes

| Guest event | `kvm_exit` observed | KVM-specific tracepoint | Returns from `KVM_RUN`? | Userspace reason | Handling location |
|---|---:|---|---:|---|---|
| Initial nested page fault | yes | `kvm_page_fault` | no | none | KVM/kernel |
| `CPUID` | yes | `kvm_cpuid` | no | none | KVM/kernel |
| `OUT 0xe9, AL` | yes | `kvm_pio` | yes | `KVM_EXIT_IO` | userspace VMM |
| `HLT` | yes | — | yes | `KVM_EXIT_HLT` | userspace VMM |

The important rule is:

> A VM exit is a transition into KVM. A userspace exit is a later decision by KVM to return control to the VMM.

---

## CPUID: VM Exit Without Userspace Return

The CPUID workload is:

```asm
mov eax, 0
cpuid
hlt
```

Its machine-code layout is:

```text
0x0: 66 b8 00 00 00 00    mov eax, 0
0x6: 0f a2                cpuid
0x8: f4                   hlt
```

The VMM configured the vCPU CPUID model by retrieving the supported CPUID table with `KVM_GET_SUPPORTED_CPUID` and installing it on the vCPU with `KVM_SET_CPUID2`.

The userspace result was:

```text
workload=cpuid iterations=1 ... userspace_exits=1 io_exits=0 hlt_exits=1
```

The KVM trace contained:

```text
kvm_entry                 rip=0x0
kvm_exit                  reason=npf rip=0x0
kvm_page_fault            rip=0x0

kvm_entry                 rip=0x0
kvm_exit                  reason=cpuid rip=0x6
kvm_cpuid                 func=0 idx=0 ...

kvm_entry                 rip=0x8
kvm_exit                  reason=idle-halt rip=0x8
kvm_userspace_exit        reason=KVM_EXIT_HLT (5)
```

Therefore the guest caused three VM exits, but only one return from `KVM_RUN` to userspace.

The CPUID exit was handled inside KVM and the guest was re-entered at RIP `0x8`. The userspace VMM never received a CPUID exit reason.

This is why CPUID must not be described as `KVM_EXIT_CPUID` in this VMM.

---

## PIO: VM Exit That Does Return to Userspace

The PIO workload executes 10,000 byte-wide writes to port `0xe9`, followed by `HLT`.

The VMM validates each I/O exit as:

```text
direction = OUT
port      = 0xe9
size      = 1
count     = 1
value     = 42
```

The userspace counters were:

```text
workload=pio iterations=10000 ... userspace_exits=10001 io_exits=10000 hlt_exits=1
```

A representative trace sequence for one PIO operation was:

```text
kvm_entry
kvm_exit                  reason=io rip=0x5
kvm_pio                   pio_write at 0xe9 size 1 count 1
kvm_userspace_exit        reason=KVM_EXIT_IO (2)
```

The full trace counts matched the VMM counters exactly:

```text
kvm_pio             = 10000
kvm_userspace_exit  = 10001
```

The `10001` userspace exits consist of:

```text
10000 x KVM_EXIT_IO
    1 x KVM_EXIT_HLT
--------------------
10001 userspace exits
```

This is the opposite of the CPUID case: KVM does not completely handle the PIO operation internally, so `KVM_RUN` returns and the userspace VMM participates in emulation.

---

## `kvm_stat` Snapshot

A one-second `kvm_stat` tracepoint snapshot during the PIO workload produced:

```text
kvm_entry                                      10010
kvm_exit                                       10010
kvm_pio                                        10000
kvm_userspace_exit                             10001
```

The two strongest checks are exact:

```text
kvm_pio            == 10000
kvm_userspace_exit == 10001
```

`kvm_entry` and `kvm_exit` are higher because KVM can enter and leave the guest for reasons that never propagate to userspace. The snapshot was aggregate over the observation window, so the extra exits are not individually attributed here.

---

## Workload Comparison

| Workload | Guest work | Userspace exits observed | Main point |
|---|---|---:|---|
| `hlt` | one `HLT` | 1 | terminal userspace exit |
| `loop` | 10,000 guest loop iterations + `HLT` | 1 | guest work does not imply userspace exits |
| `cpuid` | one `CPUID` + `HLT` | 1 | CPUID can VM-exit and still be handled inside KVM |
| `pio` | 10,000 `OUT` + `HLT` | 10,001 | PIO requires userspace handling in this VMM |

The `loop` and `pio` workloads are especially useful as a contrast: both perform 10,000 iterations, but only PIO forces 10,000 userspace I/O exits.

---

## Observability Tools Used

### VMM counters

The VMM records:

- `userspace_exits`
- `io_exits`
- `hlt_exits`
- workload runtime

These counters describe what the userspace VMM can observe directly.

### `perf stat`

Used to count KVM tracepoints and verify aggregate event counts.

### `perf record` + `perf script`

Used to preserve event ordering and RIP values. This was necessary to distinguish the sequence:

```text
entry -> VM exit -> KVM internal handling -> re-entry
```

from:

```text
entry -> VM exit -> userspace exit -> KVM_RUN returns
```

### `kvm_stat`

Used as an independent KVM-specific aggregate view of tracepoint counts.

On this host, tracefs is mounted at:

```text
/sys/kernel/tracing
```

The local `kvm_stat` script originally expected tracing under debugfs, so its tracefs path had to be adjusted before `-t` mode worked.

---

## Main Conclusion

The central Week 2 result is:

> **A VM exit does not necessarily imply a return from `KVM_RUN` to userspace.**

Observed examples:

```text
NPF   -> VM exit -> handled by KVM -> guest resumes
CPUID -> VM exit -> handled by KVM -> guest resumes
PIO   -> VM exit -> KVM_EXIT_IO  -> userspace VMM
HLT   -> VM exit -> KVM_EXIT_HLT -> userspace VMM
```

The distinction must be preserved when discussing exit counts, tracing results, or VMM performance.
