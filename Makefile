# until gcc has "#embed" support
CC=clang
CFLAGS=-std=c23 -O3 -Wpedantic -Wall -Wextra -g

.PHONY: all
all: kvm_sectorlisp

.PHONY: clean
clean:
	$(RM) kvm_sectorlisp sectorlisp.bin kvm_sectorlisp_patched
	$(MAKE) -C sectorlisp clean

kvm_sectorlisp: kvm_sectorlisp.c sectorlisp.bin
	$(CC) kvm_sectorlisp.c -o kvm_sectorlisp $(CFLAGS)

sectorlisp.bin: sectorlisp
	ln -sf sectorlisp/sectorlisp.bin sectorlisp.bin

.PHONY: sectorlisp
sectorlisp:
	$(MAKE) -C sectorlisp sectorlisp.bin

# Patches the elf interpreter to general linux from NixOS
kvm_sectorlisp_patched: kvm_sectorlisp
	cp kvm_sectorlisp kvm_sectorlisp_patched
	patchelf --set-interpreter /usr/lib64/ld-linux-x86-64.so.2 kvm_sectorlisp_patched
