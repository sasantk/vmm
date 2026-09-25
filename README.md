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

## PIO Userspace-Exit Benchmark

Week 2 adds the first repeatable userspace-exit benchmark to the minimal KVM VMM.

The guest executes 10,000 scalar `OUT` instructions to I/O port `0xe9`,
then terminates with `HLT`.

The guest workload is equivalent to:

```asm
mov cx, 10000
mov al, 42

again:
    out 0xe9, al
    loop again

hlt
```

For every `KVM_EXIT_IO`, the VMM validates:

- I/O direction
- I/O size
- I/O port
- I/O count
- output value

A successful run must produce exactly:

```text
io_exits        = 10000
hlt_exits       = 1
userspace_exits = 10001
```

The VMM prints one machine-readable result per successful run:

```text
workload=pio iterations=10000 runtime_ns=... userspace_exits=10001 io_exits=10000 hlt_exits=1
```

A non-zero process exit code indicates an invalid or failed run.

### Timing scope

The benchmark uses `CLOCK_MONOTONIC_RAW`.

The timed region starts immediately before the first `KVM_RUN` and ends
after the final return from the KVM execution loop.

The following operations are outside the timed region:

- opening `/dev/kvm`
- creating the VM
- allocating and registering guest memory
- creating the vCPU
- mapping `struct kvm_run`
- configuring registers
- cleanup

Therefore, `runtime_ns` measures the complete execution of the PIO
userspace-exit workload, including guest execution, VM exits, KVM handling,
returns to userspace, userspace validation, and re-entry into the guest.

It must not be interpreted as raw hardware VM-exit latency.

### Running the benchmark

Run the complete benchmark with:

```bash
make bench
```

By default, the benchmark is pinned to host CPU 2.

A different CPU can be selected with:

```bash
make bench CPU=6
```

The benchmark runner performs:

```text
5 warm-up runs
30 measured runs
```

Each measured run must exit successfully before it is recorded.

### Raw data

Raw benchmark results are stored under:

```text
data/raw/
```

Each CSV contains:

```text
run_id
workload
iterations
runtime_ns
userspace_exits
io_exits
hlt_exits
host_cpu
git_commit
```

Example:

```csv
run_id,workload,iterations,runtime_ns,userspace_exits,io_exits,hlt_exits,host_cpu,git_commit
1,pio,10000,45611540,10001,10000,1,2,ae9b50d
2,pio,10000,45544584,10001,10000,1,2,ae9b50d
```

### Statistical summary

After collecting the raw measurements, the benchmark automatically
generates a summary under:

```text
data/processed/
```

The summary contains:

- minimum
- median
- p95
- Q1
- Q3
- IQR
- maximum

The percentile calculation uses linear interpolation.

A representative CPU 2 run produced:

```text
runs      30
iterations 10000

min       45.298 ms
median    45.535 ms
p95       45.988 ms
Q1        45.479 ms
Q3        45.648 ms
IQR        0.169 ms
max       46.112 ms
```

The corresponding Git commit was:

```text
ae9b50d
```

### CPU pinning

The initial unpinned benchmark showed a bimodal runtime distribution,
with measurements clustering around approximately 46-48 ms and 68-70 ms.

The host contains CPU cores with different maximum frequencies.

Pinning the VMM to CPU 2 produced a tight distribution around 45.5 ms,
while pinning it to CPU 6 produced a tight distribution around 69 ms.

CPU pinning is therefore used for the primary benchmark to avoid
scheduler placement across different CPU classes from dominating the
measurement.

### perf

Additional `perf stat` measurements are stored under:

```text
reports/perf/
```

Examples include:

```text
reports/perf/pio-cpu2.txt
reports/perf/pio-cpu6.txt
reports/perf/pio-cpu2-host-guest.txt
```

On this host, `perf_event_paranoid=2` restricts unprivileged hardware
performance counters to userspace.

Privileged measurements were therefore also used to inspect host and
guest execution separately.

Example host-side counters:

```text
164,439,196 cycles:H
33,557,362 instructions:H
```

Example guest-side counters from a separate run:

```text
6,938,946 cycles:G
20,010 instructions:G
```

Because the host and guest counters above were collected in separate
runs, they must not be interpreted as components of one exact
measurement.

### Reproducibility

The benchmark records the current Git commit in every measured row.

If the working tree contains uncommitted changes, the runner appends
`-dirty` to the recorded commit identifier.

Environment information for the Week 2 benchmark is stored in:

```text
reports/week-02-environment.txt
```

Later weeks will extend this VMM with additional VM exits, guest I/O, and
other virtualization features.
