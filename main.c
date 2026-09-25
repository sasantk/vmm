#include <asm/kvm.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static const uint8_t guest_hlt[] = {
    0xf4,
};

static const uint8_t guest_loop[] = {
    0xb9, 0x10, 0x27, /* mov cx, 10000 */
    0x90,             /* nop */
    0xe2, 0xfd,       /* loop -3 */
    0xf4,             /* hlt */
};

static const uint8_t guest_pio[] = {
    0xb9, 0x10, 0x27, /* mov cx, 10000 */
    0xb0, 0x2a,       /* mov al, 42 */
    0xe6, 0xe9,       /* out 0xe9, al */
    0xe2, 0xfc,       /* loop -4 */
    0xf4,             /* hlt */
};

static const uint8_t guest_cpuid[] = {
    0x66, 0xb8, 0x00, 0x00, 0x00, 0x00, /* mov eax, 0 */
    0x0f, 0xa2,                         /* cpuid */
    0xf4,                               /* hlt */
};

struct workload {
  const char *name;

  const uint8_t *code;
  size_t code_size;

  uint64_t iterations;

  uint64_t expected_userspace_exits;
  uint64_t expected_io_exits;
  uint64_t expected_hlt_exits;
};

static const struct workload workloads[] = {
    {
        .name = "hlt",
        .code = guest_hlt,
        .code_size = sizeof(guest_hlt),
        .iterations = 1,
        .expected_userspace_exits = 1,
        .expected_io_exits = 0,
        .expected_hlt_exits = 1,
    },
    {
        .name = "loop",
        .code = guest_loop,
        .code_size = sizeof(guest_loop),
        .iterations = 10000,
        .expected_userspace_exits = 1,
        .expected_io_exits = 0,
        .expected_hlt_exits = 1,
    },
    {
        .name = "pio",
        .code = guest_pio,
        .code_size = sizeof(guest_pio),
        .iterations = 10000,
        .expected_userspace_exits = 10001,
        .expected_io_exits = 10000,
        .expected_hlt_exits = 1,
    },
    {
        .name = "cpuid",
        .code = guest_cpuid,
        .code_size = sizeof(guest_cpuid),
        .iterations = 1,
        .expected_userspace_exits = 1,
        .expected_io_exits = 0,
        .expected_hlt_exits = 1,
    },
};

int main(int argc, char **argv) {

  int status = EXIT_FAILURE;
  int ret = -1;
  int kvm_fd = -1;
  int vm_fd = -1;
  int register_mem = -1;
  int vcpu_fd = -1;
  int cpuid_set_ret = -1;
  int vcpu_size = -1;
  int vcpu_sregs = -1;
  int kvm_set_reg_ret = -1;
  int kvm_set_sreg_ret = -1;
  void *addr = MAP_FAILED;
  void *vcpu_addr = MAP_FAILED;
  struct kvm_cpuid2 *cpuid = NULL;
  size_t nent = 100;

  const struct workload *workload = NULL;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <hlt|loop|pio|cpuid>\n", argv[0]);
    return EXIT_FAILURE;
  }

  for (size_t i = 0; i < sizeof(workloads) / sizeof(workloads[0]); i++) {
    if (strcmp(argv[1], workloads[i].name) == 0) {
      workload = &workloads[i];
      break;
    }
  }

  if (workload == NULL) {
    fprintf(stderr, "unknown workload: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  kvm_fd = open("/dev/kvm", O_CLOEXEC | O_RDWR);
  if (kvm_fd < 0) {
    perror("err on openning /dev/kvm");
    goto cleanup;
  }
  ret = ioctl(kvm_fd, KVM_GET_API_VERSION, NULL);
  if (ret < 0) {
    perror("err happens on ioctl");
    goto cleanup;
  }
  if (ret != KVM_API_VERSION) {
    fprintf(stderr, "Unsupported Version");
    goto cleanup;
  }
  // printf("%d\n", ret);

  cpuid = calloc(1, sizeof(struct kvm_cpuid2) +
                        nent * sizeof(struct kvm_cpuid_entry2));
  if (cpuid == NULL) {
    perror("malloc cpuid");
    goto cleanup;
  }
  cpuid->nent = nent;
  int cpuid_ret = ioctl(kvm_fd, KVM_GET_SUPPORTED_CPUID, cpuid);
  if (cpuid_ret < 0) {
    perror("KVM_GET_SUPPORTED_CPUID");
    goto cleanup;
  }

  // printf("cpuid nent: %u\n", cpuid->nent);
  // for (uint32_t i = 0; i < cpuid->nent && i < 3; i++) {
  //   struct kvm_cpuid_entry2 *e = &cpuid->entries[i];

  //   printf("entry[%u]: function=0x%x index=0x%x "
  //          "eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x\n",
  //          i, e->function, e->index, e->eax, e->ebx, e->ecx, e->edx);
  // }

  vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
  if (vm_fd < 0) {
    perror("err happens on KVM_CREATE_VM");
    goto cleanup;
  }
  // printf("%d\n", vm_fd);
  addr = mmap(NULL, 32 * 1024, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED) {
    perror("err happens on addr mmaping");
    goto cleanup;
    // return EXIT_FAILURE;
  }

  struct kvm_userspace_memory_region mem =
      (struct kvm_userspace_memory_region){.slot = 0,
                                           .guest_phys_addr = 0x0,
                                           .memory_size = 32 * 1024,
                                           .userspace_addr = (uintptr_t)addr};

  // uint8_t sampleCode[] = {0xb9, 0x10, 0x27, 0xb0, 0x2a,
  //                         0xe6, 0xe9, 0xe2, 0xfc, 0xf4};
  // uint8_t sampleCode[] = {0xb9, 0x10, 0x27, 0x90, 0xe2, 0xfd, 0xf4};
  memcpy(addr, workload->code, workload->code_size);
  // *(uint8_t *)addr = *sampleCode;

  register_mem = ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem);
  if (register_mem < 0) {
    perror("err happens on KVM_SET_USER_MEMORY_REGION ");
    goto cleanup;
  }
  vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
  if (vcpu_fd < 0) {
    perror("err happens on KVM_CREATE_VCPU");
    goto cleanup;
  }

  cpuid_set_ret = ioctl(vcpu_fd, KVM_SET_CPUID2, cpuid);
  if (cpuid_set_ret < 0) {
    perror("err happens on KVM_SET_CPUID2");
    goto cleanup;
  }

  vcpu_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, NULL);
  if (vcpu_size < 0) {
    perror("err happens on querying KVM_GET_VCPU_MMAP_SIZE");
    goto cleanup;
  }
  if ((size_t)vcpu_size < sizeof(struct kvm_run)) {
    fprintf(stderr, "vcpu mmap size is smaller than struct kvm_run\n");
    goto cleanup;
  }
  vcpu_addr =
      mmap(NULL, vcpu_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu_fd, 0);
  if (vcpu_addr == MAP_FAILED) {
    perror("err happens on vcpu_addr");
    goto cleanup;
  }
  // if (register_vcpu < 0) {
  //   perror("err happens on registering vcpu");
  //   return EXIT_FAILURE;
  // }

  struct kvm_sregs sreg = (struct kvm_sregs){0};
  vcpu_sregs = ioctl(vcpu_fd, KVM_GET_SREGS, &sreg);
  if (vcpu_sregs < 0) {
    perror("err happens on querying vcpu_sregs");
    goto cleanup;
  }
  sreg.cs.selector = 0;
  sreg.cs.base = 0;

  struct kvm_regs reg = (struct kvm_regs){.rip = 0, .rflags = 0x2};
  kvm_set_reg_ret = ioctl(vcpu_fd, KVM_SET_REGS, &reg);
  if (kvm_set_reg_ret < 0) {
    perror("err happens on querying KVM_SET_REGS");
    goto cleanup;
  }

  kvm_set_sreg_ret = ioctl(vcpu_fd, KVM_SET_SREGS, &sreg);
  if (kvm_set_sreg_ret < 0) {
    perror("err happens on querying KVM_SET_SREGS");
    goto cleanup;
    // return EXIT_FAILURE;
  }
  // For Day4.
  struct kvm_run *kvm_run_vcpu = vcpu_addr;
  bool running = true;
  uint64_t userspace_exits = 0;
  uint64_t io_exits = 0;
  uint64_t hlt_exits = 0;
  bool io_validation_ok = true;
  struct timespec start;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &start) == -1) {
    perror("clock_gettime");
    goto cleanup;
  }
  while (running) {

    int run_vcpu = ioctl(vcpu_fd, KVM_RUN, 0);
    if (run_vcpu < 0) {
      perror("err happens on reg KVM_RUN");
      goto cleanup;
      // return EXIT_FAILURE;
    }
    // printf("%d\n", kvm_run_vcpu->exit_reason);
    switch (kvm_run_vcpu->exit_reason) {
    default:
      running = false;
      printf("Unexpected Exit: %u\n", kvm_run_vcpu->exit_reason);
      break;
    case KVM_EXIT_HLT:
      running = false;
      hlt_exits++;
      break;
    case KVM_EXIT_IO: {
      uint8_t *out = (uint8_t *)vcpu_addr + kvm_run_vcpu->io.data_offset;
      uint8_t size = kvm_run_vcpu->io.size;
      uint8_t direction = kvm_run_vcpu->io.direction;
      uint16_t port = kvm_run_vcpu->io.port;
      uint32_t count = kvm_run_vcpu->io.count;
      // EXIT_IO happens exit-reason 2
      // EXIT_IO happens io.direction 1
      // EXIT_IO happens io.size 1
      // EXIT_IO happens io.port 233
      // EXIT_IO happens io.count 1
      // EXIT_IO happens io.data_offset 4096
      // EXIT_IO happens Out: 42
      if (strcmp(workload->name, "pio") != 0) {
        running = false;
        status = EXIT_FAILURE;
        break;
      }
      if (*out == 42 && size == 1 && direction == KVM_EXIT_IO_OUT &&
          port == 0xe9 && count == 1)
        io_exits++;
      else {
        status = EXIT_FAILURE;
        running = false;
        io_validation_ok = false;
        break;
      }

      // printf("EXIT_IO happens exit-reason %u\n", kvm_run_vcpu->exit_reason);
      // printf("EXIT_IO happens io.direction %u\n",
      // kvm_run_vcpu->io.direction); printf("EXIT_IO happens io.size %u\n",
      // kvm_run_vcpu->io.size); printf("EXIT_IO happens io.port %u\n",
      // kvm_run_vcpu->io.port); printf("EXIT_IO happens io.count %u\n",
      // kvm_run_vcpu->io.count); printf("EXIT_IO happens io.data_offset
      // %llu\n",
      //        kvm_run_vcpu->io.data_offset);
      // uint8_t *out = (uint8_t *)vcpu_addr + kvm_run_vcpu->io.data_offset;
      // printf("EXIT_IO happens Out: %u\n", *out);
      break;
    }
    }
    userspace_exits++;
  }

  struct timespec end;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &end) == -1) {
    perror("clock_gettime");
    goto cleanup;
  }

  uint64_t runtime_ns =
      (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
  // printf("io_exits: %lu\n", io_exits);
  // printf("hlt_exits: %lu\n", hlt_exits);
  // printf("userspace_exits: %lu\n", userspace_exits);
  // printf("PIO userspace-exit workload runtime: %lu\n", runtime_ns);

  // printf("workload=pio iterations=10000 runtime_ns=%lu userspace_exits=%lu "
  //        "io_exits=%lu hlt_exits=%lu\n",
  //        runtime_ns, userspace_exits, io_exits, hlt_exits);
  if (io_exits != workload->expected_io_exits ||
      hlt_exits != workload->expected_hlt_exits ||
      userspace_exits != workload->expected_userspace_exits ||
      !io_validation_ok) {
    status = EXIT_FAILURE;
  } else {
    status = EXIT_SUCCESS;
  }
  printf("workload=%s iterations=%lu runtime_ns=%lu "
         "userspace_exits=%lu io_exits=%lu hlt_exits=%lu\n",
         workload->name, workload->iterations, runtime_ns, userspace_exits,
         io_exits, hlt_exits);

cleanup:
  if (vcpu_addr != MAP_FAILED)
    if (munmap(vcpu_addr, vcpu_size) != 0) {
      perror("munmap vcpu_addr");
      status = EXIT_FAILURE;
    }
  if (vcpu_fd >= 0)
    if (close(vcpu_fd) != 0) {
      perror("close vcpu_fd");
      status = EXIT_FAILURE;
    }
  if (vm_fd >= 0)
    if (close(vm_fd) != 0) {
      perror("close vm_fd");
      status = EXIT_FAILURE;
    }
  if (addr != MAP_FAILED)
    if (munmap(addr, 32 * 1024) != 0) {
      perror("munmap addr");
      status = EXIT_FAILURE;
    }
  if (kvm_fd >= 0)
    if (close(kvm_fd) != 0) {
      perror("close kvm_fd");
      status = EXIT_FAILURE;
    }
  free(cpuid);

  return status;
}
