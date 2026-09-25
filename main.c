#include <asm/kvm.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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

  uint8_t hlt = 0xF4;
  *(uint8_t *)addr = hlt;

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
      status = EXIT_SUCCESS;
      printf("HLT happens %u\n", kvm_run_vcpu->exit_reason);
      break;
    }
  }

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
