CC=gcc
CFLAGS=-std=c17 -O1 -Wpedantic -Wall -Wextra -g

.PHONY: all
all: kvm_sectorlisp sectorlisp/sectorlisp.bin

.PHONY: clean
clean:
	$(RM) kvm_sectorlisp sectorlisp_patched.bin

sectorlisp/sectorlisp.bin:
	$(MAKE) -C sectorlisp sectorlisp.bin

kvm_sectorlisp: kvm_sectorlisp.c
	$(CC) kvm_sectorlisp.c -o kvm_sectorlisp $(CFLAGS)
