CC=gcc
CFLAGS=-std=c17 -O0 -Wpedantic -Wall -Wextra -g
PORT=0xEE

.PHONY: all
all: kvm_sectorlisp sectorlisp.rom

.PHONY: clean
clean:
	$(RM) kvm_sectorlisp sectorlisp_rom.bin sectorlisp_patched.bin

sectorlisp_patched.bin: sectorlisp/sectorlisp.S
	# TODO: Should the below be %ax, not %al? That's what it was in the original
	[ -e sectorlisp/sectorlisp.bin ] && mv sectorlisp/sectorlisp.bin sectorlisp.bin.old || true
	sed -i.old \
	    -e "s/int\t\$0x16/in ${PORT},%al/g  # keyboard in                         " \
	    -e "s/int\t\$0x10/out %al,${PORT}/g # display out                         " \
	    -e '/mov\t$0x0e,%ah/d               # no need to set cp-437 as the font   ' \
	    -e 's/partition, 1/partition, 0/g   # no need bc its being copied into mem' \
	    sectorlisp/sectorlisp.S
	cd sectorlisp; make sectorlisp.bin
	mv sectorlisp/sectorlisp.bin sectorlisp_patched.bin
	mv sectorlisp/sectorlisp.S.old sectorlisp/sectorlisp.S
	[ -e sectorlisp.bin.old ] && mv sectorlisp.bin.old sectorlisp/sectorlisp.bin || true

sectorlisp.rom: sectorlisp_patched.bin
	python pad_rom.py sectorlisp_patched.bin sectorlisp.rom

kvm_sectorlisp: kvm_sectorlisp.c
	$(CC) kvm_sectorlisp.c -o kvm_sectorlisp $(CFLAGS)
