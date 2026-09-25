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
int main(void) {

  int status = EXIT_FAILURE;
  int ret = -1;
  int kvm_fd = -1;
  int vm_fd = -1;
  int register_mem = -1;
  int vcpu_fd = -1;
  int vcpu_size = -1;
  int vcpu_sregs = -1;
  int kvm_set_reg_ret = -1;
  int kvm_set_sreg_ret = -1;
  void *addr = MAP_FAILED;
  void *vcpu_addr = MAP_FAILED;

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

  uint8_t sampleCode[] = {0xb9, 0x10, 0x27, 0xb0, 0x2a,
                          0xe6, 0xe9, 0xe2, 0xfc, 0xf4};
  memcpy(addr, sampleCode, sizeof(sampleCode));
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

  printf("workload=pio iterations=10000 runtime_ns=%lu userspace_exits=%lu "
         "io_exits=%lu hlt_exits=%lu\n",
         runtime_ns, userspace_exits, io_exits, hlt_exits);

  status = EXIT_SUCCESS;
  if (io_exits != 10000 || hlt_exits != 1 || userspace_exits != 10001 ||
      !io_validation_ok)
    status = EXIT_FAILURE;
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

  return status;
}
