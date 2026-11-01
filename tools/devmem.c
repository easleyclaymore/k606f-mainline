/*
 * devmem.c - minimal MMIO register dump tool (static aarch64).
 *
 * Reads 32-bit MMIO registers via /dev/mem + mmap, so we can inspect the DSI
 * PHY/controller state that the bootloader (ABL) left behind BEFORE the msm
 * driver re-configures them. The ABL's DSI config is known-good (it shows the
 * Tux logo), so diffing its register values against what msm programs is the
 * fastest way to find the one value we get wrong.
 *
 * Build:
 *   aarch64-linux-gnu-gcc -static -O2 -o out/probe-bin/devmem tools/devmem.c
 *
 * Usage:
 *   devmem <base-hex> [count]   dump `count` (default 32) 32-bit words
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv)
{
	unsigned long base;
	int count = 32;
	int fd;
	volatile uint32_t *regs;
	unsigned long page, off;
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <base-hex> [count]\n", argv[0]);
		return 2;
	}

	base = strtoul(argv[1], NULL, 0);
	if (argc >= 3)
		count = atoi(argv[2]);

	page = base & ~0xfffUL;
	off = base & 0xfffUL;

	fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "open /dev/mem: %s\n", strerror(errno));
		return 1;
	}

	regs = mmap(NULL, off + count * 4, PROT_READ, MAP_SHARED, fd, page);
	if (regs == MAP_FAILED) {
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	for (i = 0; i < count; i++) {
		unsigned long addr = base + i * 4;
		printf("%08lx: %08x\n", addr, regs[(off / 4) + i]);
	}

	munmap((void *)regs, off + count * 4);
	close(fd);
	return 0;
}
