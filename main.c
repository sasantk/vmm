// #include <asm/kvm.h>
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

  int register_ret = ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem);
  if (register_ret < 0) {
    perror("err happens on mem_fd");
    return EXIT_FAILURE;
  }

  close(vm_fd);
  munmap(addr, 32 * 1024);
  close(kvm_fd);
  return EXIT_SUCCESS;
}
