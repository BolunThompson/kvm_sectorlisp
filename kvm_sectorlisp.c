#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

// Must be aligned to page boundaries (increments of 0x1000) for mmap
#define RAM_SIZE 0x10000
#define GET_IO_ADDR(kvm_run) (((uint8_t *)kvm_run) + kvm_run->io.data_offset)

static const uint8_t SECTORLISP_BIN[] = {
#embed "sectorlisp/sectorlisp.bin"
};

// uncomfortable global vars
static volatile bool running = true;
static volatile int errc = 1;
static struct termios old_termios;

void handle_sigint(int signal) {
  (void)signal;
  running = false;
  errc = 128 + signal;
}

// read characters one-by-one without built-in echo
void rare_mode(void) {
  tcgetattr(STDIN_FILENO, &old_termios);
  struct termios new_termios = old_termios;
  // disable econ and line editing
  new_termios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

void cooked_mode(void) { tcsetattr(STDIN_FILENO, TCSANOW, &old_termios); }

int open_vm(int *kvm, int *vmfd, int *vcpufd) {
  *vmfd = -1;
  *vcpufd = -1;
  *kvm = open("/dev/kvm", O_RDWR);
  if (*kvm == -1) {
    warn("/dev/kvm");
    return 1;
  }

  // nominal: last kvm version change was in 2007
  int ret = ioctl(*kvm, KVM_GET_API_VERSION, 0);
  if (ret == -1) {
    warn("KVM_GET_API_VERSION");
    return 1;
  }
  if (ret != 12) {
    warnx("KVM_GET_API_VERSION %d, expected 12", ret);
    return 1;
  }
  ret = ioctl(*kvm, KVM_CHECK_EXTENSION, KVM_CAP_USER_MEMORY);
  if (ret == -1) {
    warn("KVM_CHECK_EXTENSION");
    return 1;
  }
  if (!ret) {
    warnx("Required extension KVM_CAP_USER_MEM not available");
    return 1;
  }
  *vmfd = ioctl(*kvm, KVM_CREATE_VM, 0);
  if (*vmfd == -1) {
    warn("KVM_CREATE_VM");
    return 1;
  }
  *vcpufd = ioctl(*vmfd, KVM_CREATE_VCPU, 0);
  if (*vcpufd == -1) {
    warn("KVM_CREATE_VCPU");
    return 1;
  }
  return 0;
}

// ownership of mem is passed out of the function
int map_mem(const int vmfd, void **mem) {
  // map empty memory
  // TODO: Is noreserve necessary?
  *mem = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (mem == MAP_FAILED) {
    warn("memory mmap");
    return 1;
  }
  struct kvm_userspace_memory_region region = {
      .slot = 0,
      .guest_phys_addr = 0,
      .memory_size = RAM_SIZE,
      .userspace_addr = (uint64_t)(*mem),
  };
  if (ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region) == -1) {
    warn("KVM_SET_USER_MEMORY_REGION");
    return 1;
  };

  memcpy((uint8_t *)(*mem) + 0x7c00, SECTORLISP_BIN, 512);
  return 0;
}

// passes ownership of kvm_run
int map_run(const int kvm, const int vcpufd, struct kvm_run **run,
            int *run_size) {
  *run = MAP_FAILED;
  // get size of vcpu metadata (mostly kvm_run struct)
  *run_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, NULL);
  if (*run_size == -1) {
    warn("KVM_GET_CPU_MMAP_SIZE");
    return 1;
  }
  *run = mmap(NULL, *run_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);
  if (run == MAP_FAILED) {
    err(1, "kvm_run mmap");
    return 1;
  }
  return 0;
}

int set_regs(const int vcpufd) {
  // getting sregs to maintain default values (ex: flags must be inited to 0x2)
  struct kvm_sregs sregs;
  if (ioctl(vcpufd, KVM_GET_SREGS, &sregs) == -1) {
    warn("KVM_GET_SREGS");
    return 1;
  }
  // segment where the boot sector is loaded
  sregs.cs.base = 0x7c0;
  if (ioctl(vcpufd, KVM_SET_SREGS, &sregs) == -1) {
    warn("KVM_SET_SREGS");
    return 1;
  }
  struct kvm_regs regs = {
      .rip = 0, // start at start of bootsector
      .rdi = 0, // loading as "disk 0"
  };
  if (ioctl(vcpufd, KVM_SET_REGS, &regs) == -1) {
    warn("KVM_SET_REGS");
    return 1;
  }
  return 0;
}

// runs UNTIL exit (doesn't stop at each instr).
static inline int kvm_run(int vcpufd, struct kvm_regs *regs,
                          struct kvm_sregs *sregs) {
  // IP is now set at the last ran instruction's
  if (ioctl(vcpufd, KVM_RUN, NULL) == -1) {
    warn("KVM_RUN");
    return 1;
  }
  if (ioctl(vcpufd, KVM_GET_REGS, regs) == -1) {
    warn("KVM_GET_REGS DEBUG");
    return 1;
  }
  if (ioctl(vcpufd, KVM_GET_SREGS, sregs) == -1) {
    warn("KVM_GET_SREGS DEBUG");
    return 1;
  }
  return 0;
}

// returns whether a quit condition was reached
static inline bool handle_serial_io(struct kvm_run *run) {
  if (run->io.direction == KVM_EXIT_IO_OUT) {
    putchar(*GET_IO_ADDR(run));
  } else {
    char c = getchar();
    // exit on ctrl-d
    if (c == '\4') {
      return true;
    }
    *GET_IO_ADDR(run) = c;
  }
  return false;
}

int main(void) {
  // TODO: Is this the correct formatting? It's a bit dense for my taste.
  // TODO: How to properly cite licenses?
  // https://lwn.net/Articles/658511/ and https://lwn.net/Articles/658512/
  // guided me through the KVM API -- thanks!

  signal(SIGINT, handle_sigint);

  // Make terminal worse to emulate text-mode VGA
  rare_mode();
  int kvm, vmfd, vcpufd;
  // kvm loads a standard, fresh from boot, realmode x86 system.
  if (open_vm(&kvm, &vmfd, &vcpufd))
    goto cleanup_vm;

  void *mem;
  if (map_mem(vmfd, &mem))
    goto cleanup_mem;

  if (set_regs(vcpufd))
    goto cleanup_mem;

  int run_size;
  struct kvm_run *run;
  if (map_run(kvm, vcpufd, &run, &run_size))
    goto cleanup;
  // the first ljmp jumps to 0x7c37. The disasembler doesn't list this
  // instruction -- it incorrectly disassmebles it as starting at 0x7c36.
  // (TODO: Why? Something to do with x86 encoding?)

  // on sigint, quit gracefully by setting globar var running
  while (running) {
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    if (kvm_run(vcpufd, &regs, &sregs))
      goto cleanup;
    switch (run->exit_reason) {
    case KVM_EXIT_IO:
      // size is the size of each message (1, 2 or 4 bytes).
      // count is how many messages were sent
      if (run->io.size == 1 && run->io.port == 0xEE && run->io.count == 1) {
        if (handle_serial_io(run)) {
          errc = 0;
          goto cleanup;
        }
      } else {
        warnx(
            "0x%04llx: unhandled KVM_EXIT_IO at port 0x%x with direction '%s', "
            "size %d and count %d",
            regs.rip, run->io.port, run->io.direction ? "out" : "in",
            run->io.size, run->io.count);
        goto cleanup;
      }
      break;
    case KVM_EXIT_FAIL_ENTRY:
      warnx("0x%04llx: KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = "
            "0x%llx",
            regs.rip, run->fail_entry.hardware_entry_failure_reason);
      goto cleanup;
    case KVM_EXIT_INTERNAL_ERROR:
      warnx("0x%04llx: KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x%s", regs.rip,
            run->internal.suberror,
            run->internal.suberror == 1 ? " -- Illegal instruction" : "");
      goto cleanup;
    case KVM_EXIT_MMIO:
      // This is triggered on a sectorlisp syntax error. I don't exactly know
      // why, but it's likely reading some memory (or calling an int
      // instruction) that it isn't supposed to. I use it as a hook to close the
      // program, but if it's exiting weirdly, there may be an actual bug.

      warnx("0x%04llx: unhandled KVM_EXIT_MMIO with first value %d,"
            "len %d, %%si of 0x%llx, is_write '%s'",
            regs.rip, run->mmio.data[0], run->mmio.len, regs.rsi,
            run->mmio.is_write ? "true" : "false");
      fprintf(stderr, "error: system hung, now quitting.\n");
      errc = 2;
      goto cleanup;
    case KVM_EXIT_HLT:
      warnx("0x%04llx: hlt", regs.rip);
      goto cleanup;
    default:
      warnx("0x%04llx: exit_reason = 0x%x", regs.rip, run->exit_reason);
      goto cleanup;
    }
  }
// nops if map failed
cleanup:
  munmap(run, run_size);
cleanup_mem:
  munmap(mem, RAM_SIZE);
cleanup_vm:
  close(vcpufd);
  close(vmfd);
  close(kvm);
  cooked_mode();
  return errc;
}
