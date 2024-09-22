CC=clang
CFLAGS=-std=c23 -O3 -Wpedantic -Wall -Wextra -g

.PHONY: all
all: kvm_sectorlisp sectorlisp/sectorlisp.bin

.PHONY: clean
clean:
	$(RM) kvm_sectorlisp sectorlisp/sectorlisp.bin

sectorlisp/sectorlisp.bin:
	$(MAKE) -C sectorlisp sectorlisp.bin

kvm_sectorlisp: kvm_sectorlisp.c
	$(CC) kvm_sectorlisp.c -o kvm_sectorlisp $(CFLAGS)
