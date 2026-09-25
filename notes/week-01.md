# Week 01 — First KVM Guest

## Goal

Run the first guest directly through `/dev/kvm`, without QEMU or libvirt.

The guest is intentionally minimal: it executes one `HLT` instruction and
returns control to the VMM through `KVM_EXIT_HLT`.

---

## 1. KVM object hierarchy

The program uses three important file descriptors:

```text
kvm_fd
  |
  +-- vm_fd
        |
        +-- vcpu_fd
```

### `kvm_fd`

Created by:

```text
open("/dev/kvm")
```

It represents access to the KVM subsystem.

System-level KVM ioctls are issued against this fd, for example:

```text
KVM_GET_API_VERSION
KVM_CREATE_VM
KVM_GET_VCPU_MMAP_SIZE
```

### `vm_fd`

Created with:

```text
KVM_CREATE_VM
```

It represents one virtual machine.

VM-level operations such as registering guest memory and creating vCPUs are
performed through this fd.

Examples:

```text
KVM_SET_USER_MEMORY_REGION
KVM_CREATE_VCPU
```

### `vcpu_fd`

Created with:

```text
KVM_CREATE_VCPU
```

It represents one virtual CPU belonging to the VM.

vCPU state and execution are controlled through this fd.

Examples:

```text
KVM_GET_SREGS
KVM_SET_SREGS
KVM_SET_REGS
KVM_RUN
```

---

## 2. Guest memory: GPA vs HVA

The guest does not directly use a normal userspace virtual address.

Two address spaces are involved:

```text
Guest Physical Address (GPA)
Userspace Host Virtual Address (HVA)
```

The VMM allocates memory in its own process using anonymous `mmap()`.

For Week 1:

```text
guest memory size = 32 KiB
guest_phys_addr   = 0
slot              = 0
```

The resulting userspace pointer is registered with KVM using:

```text
KVM_SET_USER_MEMORY_REGION
```

The relationship is conceptually:

```text
HVA = userspace_addr + (GPA - guest_phys_addr)
```

For this VM, because guest memory begins at GPA 0:

```text
GPA 0
  |
  v
first byte of the userspace mmap
```

The memory slot number is only an identifier for the mapping. It is not a
file descriptor or pointer.

---

## 3. The guest program

The guest program consists of exactly one byte:

```text
F4
```

`0xF4` is the x86 `HLT` instruction.

It is written to the first byte of the registered guest memory, which
corresponds to GPA `0x0`.

The vCPU is configured with:

```text
CS.selector = 0
CS.base     = 0
RIP         = 0
RFLAGS      = 0x2
```

With `CS.base = 0` and `RIP = 0`, execution begins at address 0, where the
`HLT` instruction was placed.

---

## 4. `struct kvm_run`

Each vCPU has a shared memory region used for communication between KVM and
userspace.

The required mapping size is obtained using:

```text
KVM_GET_VCPU_MMAP_SIZE
```

The returned size is used to `mmap()` the vCPU fd.

The resulting mapping is interpreted as:

```text
struct kvm_run *
```

`struct kvm_run` is not separately allocated by the VMM.

It is only a typed view over the shared vCPU mapping.

Therefore these two pointers refer to the same underlying resource:

```text
vcpu_addr
kvm_run_vcpu
```

Only one `munmap()` is required.

---

## 5. Running the vCPU

Execution is started with:

```text
KVM_RUN
```

`KVM_RUN` enters guest execution.

When the guest causes a VM exit, control returns to userspace and the reason
for the exit is available in:

```text
kvm_run->exit_reason
```

For the Week 1 guest:

```text
guest executes HLT
        |
        v
VM exit
        |
        v
KVM_RUN returns
        |
        v
exit_reason == KVM_EXIT_HLT
```

On this system, `KVM_EXIT_HLT` has numeric value `5`.

After executing the one-byte `HLT` instruction, RIP advances from:

```text
0 -> 1
```

which confirms that the guest executed the instruction at address zero.

---

## 6. Resource ownership

`main()` owns all resources created by the VMM.

| Resource | Acquire | Release |
|---|---|---|
| KVM fd | `open("/dev/kvm")` | `close()` |
| VM fd | `KVM_CREATE_VM` | `close()` |
| Guest RAM | anonymous `mmap()` | `munmap()` |
| vCPU fd | `KVM_CREATE_VCPU` | `close()` |
| vCPU shared mapping | `mmap(vcpu_fd)` | `munmap()` |

The pointers used as aliases do not represent additional ownership.

For example:

```text
kvm_run_vcpu -> vcpu_addr
```

does not require another `munmap()`.

Similarly, registering guest memory with KVM does not transfer ownership of
the userspace mapping to KVM.

The VMM remains responsible for keeping the memory alive for the lifetime of
the VM and eventually unmapping it.

---

## 7. Cleanup strategy

Resource variables are initialized with invalid sentinel values:

```text
fd      -> -1
mapping -> MAP_FAILED
```

All error paths jump to one cleanup section.

This makes partially initialized states safe.

The cleanup order is:

```text
vCPU shared mapping
        |
        v
vCPU fd
        |
        v
VM fd
        |
        v
guest memory
        |
        v
KVM fd
```

This roughly follows reverse ownership/dependency order.

Each successful resource acquisition has exactly one corresponding release.

Failures from both `munmap()` and `close()` are checked and reported.

If normal guest execution succeeds but cleanup fails, the final process
status becomes `EXIT_FAILURE`.

---

## 8. Build modes

The Makefile provides separate build modes.

### Debug

```text
-O0 -g
```

Used for development and debugging.

### Release

```text
-O2
```

Used for an optimized build.

### ASan

Built with:

```text
-fsanitize=address
-fno-omit-frame-pointer
```

ASan was manually verified by introducing a temporary heap buffer overflow
and confirming that it was detected.

### UBSan

Built with:

```text
-fsanitize=undefined
-fno-sanitize-recover=undefined
-fno-omit-frame-pointer
```

UBSan was manually verified by introducing a temporary signed integer
overflow and confirming that it was detected.

The artificial bugs were removed after verification.

---

## 9. Automated tests

### Smoke test

Runs one debug guest execution.

Expected result:

```text
KVM_EXIT_HLT
exit status 0
```

### 100-run test

Runs the VMM 100 consecutive times.

If any execution returns a non-zero exit status, the loop immediately fails
and reports the failing iteration.

### Sanitizer checks

The ASan and UBSan binaries are both executed as part of the automated test
flow.

### `make check`

The current aggregate test target runs:

```text
smoke
run100
asan-check
ubsan-check
```

---

## 10. Test harness verification

The test harness itself was intentionally tested against failures.

Changing the guest opcode from:

```text
0xF4
```

to an invalid/unexpected instruction demonstrated that the test must
propagate the guest program's non-zero exit status.

The initial implementation used:

```text
command || break
```

which stopped the loop but still allowed the shell recipe to return success.

The test was changed so a failed guest execution explicitly exits the recipe
with a non-zero status.

This ensures that a failing VMM causes `make` to fail as well.

---

## 11. Week 1 Definition of Done

- [x] `/dev/kvm` opened successfully
- [x] `KVM_GET_API_VERSION` validated
- [x] VM created
- [x] Guest memory allocated
- [x] Guest memory registered with KVM
- [x] One vCPU created
- [x] `struct kvm_run` shared area mapped
- [x] Guest registers configured
- [x] Tiny `HLT` guest executed
- [x] `KVM_EXIT_HLT` observed
- [x] Cleanup path handles partial initialization
- [x] `close()` and `munmap()` errors checked
- [x] ASan build and detection verified
- [x] UBSan build and detection verified
- [x] Smoke test implemented
- [x] 100 consecutive executions implemented
- [x] Automated `make check` target implemented
- [x] Week 1 notes completed

---

## 12. Main lessons

The most important conceptual model from Week 1 is:

```text
KVM provides the virtualization mechanism.

Userspace still owns:

- VM construction
- guest memory backing
- vCPU configuration
- guest execution policy
- VM exit handling
- resource lifetime
```

`KVM_RUN` does not mean that KVM takes over the whole VM.

It runs a vCPU until something causes execution to return to userspace.

The VMM is therefore fundamentally a loop between:

```text
configure state
     |
     v
KVM_RUN
     |
     v
VM exit
     |
     v
userspace decides what to do
```

Week 1 only handles the simplest terminal exit: `KVM_EXIT_HLT`.

Week 2 will extend this model by introducing additional VM exits and guest
I/O.
