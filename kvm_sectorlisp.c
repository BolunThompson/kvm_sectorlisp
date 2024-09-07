#include <err.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <wchar.h>

#define BIOS_PATH "coreboot.rom"
// TODO: explain line.
#define GET_IO_ADDR(kvm_run) (((uint8_t *)kvm_run) + kvm_run->io.data_offset)
// initially planned to use iconv but the API is too clunky

// taken from https://github.com/Journeyman1337/cp437.h/tree/main
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
  // return it unchanged if ascii. It's fine if it's unprintable.
  if (in <= 127)
    return (unsigned char)in;
  unsigned char c = 0;
  do {
    if (cp437_to_wchar(c) == in)
      return c;
  } while (++c);
  return '?';
}

// TODO: Is this good kvm api use? Maybe ask on the irc.
int main(int argc, char **argv) {
  // TODO: Is this the correct formatting? It's a bit dense for my taste.
  // TODO: Somehow put slisp into
  // TODO: How to properly cite licenses?
  // read sectorlisp bin
  int ret;
  // TODO: Usage?
  if (argc > 2) {
    errx(1, "0 or 1 arguments expected");
  }
  char *coreboot_path = argc == 2 ? argv[1] : BIOS_PATH;

  // https://lwn.net/Articles/658511/ and https://lwn.net/Articles/658512/
  // guided me through this -- thanks!
  int kvm = open("/dev/kvm", O_RDWR);
  if (kvm == -1)
    err(1, "/dev/kvm");

  int coreboot = open(coreboot_path, O_RDONLY);
  if (coreboot == -1)
    err(1, "%s", coreboot_path);

  // TODO: How do I check for KVM existence and the right version, properly?
  // TODO: Should I check for extensions? I use KVM_CAP_USER_MEMORY for the
  // memory region Initializes the first segment of memory with the bios. Areas
  // not used by the bios have been zeroed out.
  // TODO: Write about bios troubles.
  // Initially I thought everything is included; it's not.
  // Then I tried to figure out how to load stuff with CBFS with seabios
  int vmfd = ioctl(kvm, KVM_CREATE_VM, 0);
  if (vmfd == -1)
    err(1, "KVM_CREATE_VM");

  void *mem;
  struct kvm_userspace_memory_region region;
  // size of coreboot. The real mode payload will only have access to the first
  // 0x100000 bytes.

  // Why???
  mem = mmap(NULL, 0xffbfffff, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED)
    err(1, "memory mmap");
  region = (struct kvm_userspace_memory_region){
      .slot = 0,
      .guest_phys_addr = 0,
      .memory_size = 0xffbfffff,
      .userspace_addr = (uint64_t)mem,
  };
  ret = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
  if (ret == -1)
    err(1, "KVM_SET_USER_MEMORY_REGION memory");

  mem = mmap(NULL, 0x400000, PROT_READ | PROT_WRITE, MAP_PRIVATE, coreboot, 0);
  if (mem == MAP_FAILED)
    err(1, "BIOS mmap");
  // NOTE: in qemu memdump, coreboot starts at 0x789c4dd
  region = (struct kvm_userspace_memory_region){
      .slot = 1,
      .guest_phys_addr = 0xffbfffff,
      .memory_size = 0x400000,
      .userspace_addr = (uint64_t)mem,
  };
  ret = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
  if (ret == -1)
    err(1, "KVM_SET_USER_MEMORY_REGION bios");

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

  // rip, cs and flags default to standard x86 values (reset vector and 0x2)
  // but this is still crashing
  // Same with sregs?
  struct kvm_sregs kvm_sregs;
  ioctl(vcpufd, KVM_GET_SREGS, &kvm_sregs);

  while (true) {
    ioctl(vcpufd, KVM_RUN, NULL);
    if (ret == -1)
      err(1, "KVM_RUN");

    switch (run->exit_reason) {
    case KVM_EXIT_IO:
      // size is the size of each message (1, 2 or 4 bytes) and count is how
      // many messages were sent
      if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 2 &&
          run->io.port == 0xacf && run->io.count == 1)
        putchar(*GET_IO_ADDR(run));
      // TODO: What does the size mean? How does a pin have a size > 1?
      else if (run->io.direction == KVM_EXIT_IO_IN && run->io.size == 2 &&
               run->io.port == 0xb5 && run->io.count == 1)
        ;
      // *GET_IO_ADDR(run) = wchar_to_cp437(getwchar());
      else
        errx(1,
             "unhandled KVM_EXIT_IO at port 0x%x with direction '%s', "
             "size %d and count %d",
             run->io.port, run->io.direction ? "out" : "in", run->io.size,
             run->io.count);
      break;
    case KVM_EXIT_FAIL_ENTRY:
      errx(1, "KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx",
           run->fail_entry.hardware_entry_failure_reason);
    case KVM_EXIT_INTERNAL_ERROR:
      // 1 means an illegal instruction
      errx(1, "KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x",
           run->internal.suberror);
    default:
      errx(1, "exit_reason = 0x%x", run->exit_reason);
    }
  }

  // TODO: Cleanup?
  // TODO: How to exit properly?
}
