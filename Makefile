CC=gcc
CFLAGS=-std=c17 -O0 -Wpedantic -Wall -Wextra -g

.PHONY: all
all: kvm_sectorlisp

.PHONY: clean
clean:
	$(RM) kvm_sectorlisp

kvm_sectorlisp: kvm_sectorlisp.c
	$(CC) kvm_sectorlisp.c -o kvm_sectorlisp $(CFLAGS)
