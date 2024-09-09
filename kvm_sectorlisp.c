#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#define GET_IO_ADDR(kvm_run) (((uint8_t *)kvm_run) + kvm_run->io.data_offset)

// initially planned to use iconv but the API is too clunky
// taken from https://github.com/Journeyman1337/cp437.h/tree/main
// TODO: Dw repeated symbols
static const wchar_t *const CP_WCHAR_LOOKUP_TABLE =
    L"\0☺☻♥♦♣♠•◘○◙♂♀♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼"
    L" !\"#$%&'()*+,-./0123456789:;<=>?@ABC"
    L"DEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefg"
    L"hijklmnopqrstuvwxyz{|}~⌂Çüéâäàåçêëèïî"
    L"ìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒"
    L"▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓"
    L"╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ";

static inline wchar_t cp437_to_wchar(unsigned char c) {
  return CP_WCHAR_LOOKUP_TABLE[c];
}

static inline unsigned char wchar_to_cp437(wchar_t in) {
  // return unchanged if ascii. It's fine if it was unprintable.
  if (in <= 127)
    return in;
  unsigned char c = 0;
  do {
    if (cp437_to_wchar(c) == in)
      return c;
  } while (++c);
  return '?';
}

// TODO: Is this good kvm api use? Maybe ask on the irc.
int main(void) {
  // TODO: Is this the correct formatting? It's a bit dense for my taste.
  // TODO: How to properly cite licenses?
  int ret;
  // https://lwn.net/Articles/658511/ and https://lwn.net/Articles/658512/
  // guided me through this -- thanks!
  int kvm = open("/dev/kvm", O_RDWR);
  if (kvm == -1)
    err(1, "/dev/kvm");

  int sectorlisp = open("sectorlisp.rom", O_RDONLY);
  if (sectorlisp == -1)
    err(1, "sectorlisp.rom");

  // TODO: How do I check for KVM existence and the right version, properly?
  // TODO: Should I check for extensions? I use KVM_CAP_USER_MEMORY for the
  // memory region.
  int vmfd = ioctl(kvm, KVM_CREATE_VM, 0);
  if (vmfd == -1)
    err(1, "KVM_CREATE_VM");

  // map 64 KiB padded sectorlisp as memory
  void *mem =
      mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE, sectorlisp, 0);
  if (mem == MAP_FAILED)
    err(1, "memory mmap");
  struct kvm_userspace_memory_region region = {
      .slot = 0,
      .guest_phys_addr = 0x00000,
      // Must be aligned to page boundaries! (increments of 0x1000)
      .memory_size = 0x10000,
      .userspace_addr = (uint64_t)mem,
  };
  ret = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
  if (ret == -1)
    err(1, "KVM_SET_USER_MEMORY_REGION");

  // by default this just loads standard realmode x86 system.
  // get vcpu
  int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);
  if (vcpufd == -1)
    err(1, "KVM_CREATE_VCPU");

  // get size of vcpu metadata (mostly kvm_run struct)
  int mmap_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, NULL);
  if (mmap_size == -1)
    err(1, "KVM_GET_CPU_MMAP_SIZE");
  struct kvm_run *run =
      mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);
  if (run == MAP_FAILED)
    err(1, "kvm_run mmap");

  // Interestingly, sgabios was built by a google engineer for early boot
  // debugging of (now legacy) datacenter systems!

  // I'm getting the sregs because KVM sets them to special values on init
  // The main one is setting the flags register to 0x2 (required by x86)
  // I'm changing them to set the cs and ip regs to where I loaded sectorlisp
  // in memory
  struct kvm_sregs sregs;
  ret = ioctl(vcpufd, KVM_GET_SREGS, &sregs);
  if (ret == -1)
    err(1, "KVM_GET_SREGS");
  // segment where the boot sector is loaded
  sregs.cs.base = 0x7c0;
  // TODO: is this necessary? I assume the defaults are sane, and we're only
  // running one program.
  // where the segment is in the global descriptor table
  sregs.cs.selector = 0;
  ret = ioctl(vcpufd, KVM_SET_SREGS, &sregs);
  if (ret == -1)
    err(1, "KVM_SET_SREGS");

  struct kvm_regs regs;
  ret = ioctl(vcpufd, KVM_GET_REGS, &regs);
  // TODO: regs.rdx is set to 0x600 by default -- why?
  if (ret == -1)
    err(1, "KVM_GET_REGS");
  // TODO: remove if unncessary
  regs.rip = 0; // start of program
  regs.rdi = 0; // disk number

  // TODO: does it do anything to disable interrupts? I don't think so
  // could an interrupt be called randomly by the cpu and cause problms b/c of a
  // zeroed IDT? flags default to 0x2, which means that interrupts are disabled
  // (and the CPU's in ring 0)
  ret = ioctl(vcpufd, KVM_SET_REGS, &regs);
  if (ret == -1)
    err(1, "KVM_SET_REGS");

  // TODO: how to unregister mmio?
  // if coalesced mmio exists
  // Doesn't work:
  // struct kvm_coalesced_mmio_zone mmio = {
  //     .addr = 0x00000,
  //     .size = 0x10000,
  // };
  // ret = ioctl(vmfd, KVM_UNREGISTER_COALESCED_MMIO, &mmio);
  // if (ret == -1)
  //   err(1, "KVM_UNREGISTER_COALESCED_MMIO");
  // Try 2, kvm_pre_fault_memory, doesn't work b/c it's not supported in my linux
  // kernel version

  // the first ljmp jumps to 0x7c37. The disasembler doesn't list this
  // instruction -- it incorrectly disassmebles it as starting at 0x7c36. (TODO:
  // Why? Something to do with x86 encoding?)

  while (true) {
    // runs UNTIL exit (doesn't stop at each instr).
    ret = ioctl(vcpufd, KVM_RUN, NULL);
    // IP is now set at the last ran instruction's
    if (ret == -1)
      err(1, "KVM_RUN");
    ret = ioctl(vcpufd, KVM_GET_REGS, &regs);
    if (ret == -1)
      err(1, "KVM_GET_REGS 2");

    ret = ioctl(vcpufd, KVM_GET_SREGS, &sregs);
    if (ret == -1)
      err(1, "KVM_GET_SREGS 2");

    switch (run->exit_reason) {
    case KVM_EXIT_IO:
      // size is the size of each message (1, 2 or 4 bytes).
      // count is how many messages were sent
      if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 &&
          run->io.port == 0xEE && run->io.count == 1) {
        printf("put_io\n");
        putwchar(*GET_IO_ADDR(run));
      } else if (run->io.direction == KVM_EXIT_IO_IN && run->io.size == 1 &&
                 run->io.port == 0xEE && run->io.count == 1) {
        printf("get_io\n");
        *GET_IO_ADDR(run) = wchar_to_cp437(getwchar());
      } else
        errx(
            1,
            "0x%04llx: unhandled KVM_EXIT_IO at port 0x%x with direction '%s', "
            "size %d and count %d",
            regs.rip, run->io.port, run->io.direction ? "out" : "in",
            run->io.size, run->io.count);
      break;
    case KVM_EXIT_FAIL_ENTRY:
      errx(1,
           "0x%04llx: KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = "
           "0x%llx",
           regs.rip, run->fail_entry.hardware_entry_failure_reason);
    case KVM_EXIT_INTERNAL_ERROR:
      errx(1, "0x%04llx: KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x%s", regs.rip,
           run->internal.suberror,
           run->internal.suberror == 1 ? " -- Illegal instruction" : "");
    case KVM_EXIT_MMIO:
      // TODO: do something with this. The only instr accessing this is a null
      // op rn Ideally, I could ioctl kvm into not believing this is mmio. Else,
      // just suppress this error like how I am doing
      // TODO: Why? See https://www.kernel.org/doc/html/v5.7/virt/kvm/mmu.html
      warnx("0x%04llx: unhandled KVM_EXIT_MMIO with first value %d, phys addr "
            "of 0x%llx, "
            "len %d, %%si of 0x%llx, is_write '%s'",
            regs.rip, run->mmio.data[0], run->mmio.phys_addr, run->mmio.len,
            regs.rsi, run->mmio.is_write ? "true" : "false");
      break;
    default:
      errx(1, "0x%04llx: exit_reason = 0x%x", regs.rip, run->exit_reason);
    }
  }
  // TODO: Cleanup? Do I need to?
  return 0;
}
