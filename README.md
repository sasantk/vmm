# kvm-sample

A minimal userspace VMM built directly on top of `/dev/kvm`.

The goal of this project is to learn the KVM userspace API by building a
small virtual machine monitor without QEMU or libvirt.

The current implementation:

- opens `/dev/kvm`
- validates the KVM API version
- creates a VM
- allocates and registers guest physical memory
- creates one vCPU
- maps the shared `struct kvm_run` area
- configures the guest registers
- executes a one-byte guest program containing `HLT`
- handles `KVM_EXIT_HLT`
- performs explicit resource cleanup

## Requirements

- Linux with KVM support
- access to `/dev/kvm`
- Clang
- GNU Make

Verify KVM access with:

```bash
ls -l /dev/kvm
```

## Build

Default optimized build:

```bash
make
```

Release build:

```bash
make release
```

Debug build:

```bash
make debug
```

The explicit build targets place their binaries under `bin/`.

## Sanitizers

AddressSanitizer build:

```bash
make asan
```

UndefinedBehaviorSanitizer build:

```bash
make ubsan
```

UBSan is built with `-fno-sanitize-recover=undefined` so undefined behavior
causes the test to fail instead of only printing a diagnostic.

## Tests

Run one basic guest execution:

```bash
make smoke
```

Run the guest 100 consecutive times:

```bash
make run100
```

Run the automated Week 1 checks:

```bash
make check
```

The current `check` target includes:

- smoke test
- 100-run test
- AddressSanitizer check
- UndefinedBehaviorSanitizer check

A successful guest execution reaches `KVM_EXIT_HLT` and exits with status 0.

## Guest

The Week 1 guest is intentionally minimal.

Guest physical address `0x0` contains a single x86 instruction:

```text
0xF4    HLT
```

The vCPU starts with:

```text
CS.base     = 0
CS.selector = 0
RIP         = 0
RFLAGS      = 0x2
```

Therefore the first instruction fetched by the guest is the `HLT` byte at
guest physical address 0.

Executing it causes the vCPU to leave guest mode and return to userspace with:

```text
KVM_EXIT_HLT
```

## Project status

Week 1 implements the minimum KVM lifecycle:

```text
/dev/kvm
   |
   +-- VM
        |
        +-- guest memory
        |
        +-- vCPU
             |
             +-- kvm_run shared mapping
```

Later weeks will extend this VMM with additional VM exits, guest I/O, and
other virtualization features.
