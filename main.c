// #include <asm/kvm.h>
#include <asm/kvm.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {

  int kvm_fd = open("/dev/kvm", O_CLOEXEC | O_RDWR);
  if (kvm_fd < 0) {
    perror("err on openning /dev/kvm");
    return EXIT_FAILURE;
  }
  int ret = ioctl(kvm_fd, KVM_GET_API_VERSION, NULL);
  if (ret < 0) {
    perror("err happens on ioctl");
    return EXIT_FAILURE;
  }
  if (ret != KVM_API_VERSION) {
    fprintf(stderr, "Unsupported Version");
    return EXIT_FAILURE;
  }
  printf("%d\n", ret);

  int vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
  if (vm_fd < 0) {
    perror("err happens on ioctl");
    return EXIT_FAILURE;
  }
  printf("%d\n", vm_fd);
  void *addr = mmap(NULL, 32 * 1024, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED) {
    perror("err happens on mmaping");
    return EXIT_FAILURE;
  }

  struct kvm_userspace_memory_region mem =
      (struct kvm_userspace_memory_region){.slot = 0,
                                           .guest_phys_addr = 0x0,
                                           .memory_size = 32 * 1024,
                                           .userspace_addr = (uintptr_t)addr};

  int register_mem = ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem);
  if (register_mem < 0) {
    perror("err happens on mem_fd");
    return EXIT_FAILURE;
  }

  int vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
  if (vcpu_fd < 0) {
    perror("err happens on vcpu_fd");
    return EXIT_FAILURE;
  }

  int vcpu_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, NULL);
  if (vcpu_size < 0) {
    perror("err happens on querying vcpu size");
    return EXIT_FAILURE;
  }
  void *vcpu_addr =
      mmap(NULL, vcpu_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu_fd, 0);
  if (vcpu_addr == MAP_FAILED) {
    perror("err happens on vcpu mapping");
    return EXIT_FAILURE;
  }
  // if (register_vcpu < 0) {
  //   perror("err happens on registering vcpu");
  //   return EXIT_FAILURE;
  // }

  struct kvm_sregs sreg = (struct kvm_sregs){};
  int vcpu_sregs = ioctl(vcpu_fd, KVM_GET_SREGS, &sreg);
  if (vcpu_sregs < 0) {
    perror("err happens on querying vcpu_sregs");
    return EXIT_FAILURE;
  }
  sreg.cs.selector = 0;
  sreg.cs.base = 0;

  struct kvm_regs reg = (struct kvm_regs){.rip = 0, .rflags = 0x2};
  int kvm_set_reg_ret = ioctl(vcpu_fd, KVM_SET_REGS, &reg);
  if (kvm_set_reg_ret < 0) {
    perror("err happens on querying vcpu size");
    return EXIT_FAILURE;
  }

  int kvm_set_sreg_ret = ioctl(vcpu_fd, KVM_SET_SREGS, &sreg);
  if (kvm_set_sreg_ret < 0) {
    perror("err happens on querying vcpu size");
    return EXIT_FAILURE;
  }
  // For Day4.
  // struct kvm_run *kvm_run_vcpu = vcpu_addr;
  int run_vcpu = ioctl(vcpu_fd, KVM_RUN, NULL);
  if (run_vcpu < 0) {
    perror("err happens on reg vcpu");
    return EXIT_FAILURE;
  }
  // ###
  munmap(vcpu_addr, vcpu_size);
  close(vcpu_fd);
  close(vm_fd);
  munmap(addr, 32 * 1024);
  close(kvm_fd);
  return EXIT_SUCCESS;
}
