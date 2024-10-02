# until gcc has "#embed" support
CC=clang
CFLAGS=-std=c23 -O3 -Wpedantic -Wall -Wextra -g

.PHONY: all
all: kvm_sectorlisp

.PHONY: clean
clean:
	$(RM) kvm_sectorlisp sectorlisp.bin
	$(MAKE) -C sectorlisp clean

kvm_sectorlisp: kvm_sectorlisp.c sectorlisp.bin
	$(CC) kvm_sectorlisp.c -o kvm_sectorlisp $(CFLAGS)

sectorlisp.bin: sectorlisp
	ln -sf sectorlisp/sectorlisp.bin sectorlisp.bin

.PHONY: sectorlisp
sectorlisp:
	$(MAKE) -C sectorlisp sectorlisp.bin

