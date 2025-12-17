/*
 * dax_pmd_test.c - Reproduce FS-DAX large folio warning
 *
 * This program reproduces the spurious VM_WARN_ON_ONCE(folio_test_large(folio))
 * warning in mm/memremap.c:free_zone_device_folio() that triggers when FS-DAX
 * uses PMD (2MB) mappings.
 *
 * The bug was introduced by a commit that added support for large zone device
 * private folios, but did not account for FS-DAX file-backed folios which have
 * always supported large (PMD-sized) mappings.
 *
 * Prerequisites:
 *   - A system with pmem (persistent memory) or emulated pmem
 *   - An fsdax namespace: ndctl create-namespace -m fsdax -e namespace0.0
 *   - XFS must be created with 2MB stripe alignment to ensure PMD faults
 *     succeed (otherwise they fall back to PTE faults and the bug is hidden)
 *
 * Quick start:
 *   make test    # Does everything automatically (requires sudo)
 *
 * Manual steps:
 *   1. Create XFS with 2MB stripe alignment:
 *      # mkfs.xfs -f -d su=2m,sw=1 /dev/pmem0
 *      # mount -o dax /dev/pmem0 /mnt/pmem
 *
 *   2. Run this test:
 *      $ ./dax_pmd_test /mnt/pmem/testfile
 *
 *   3. Check dmesg for:
 *      WARNING: mm/memremap.c:431 at free_zone_device_folio+0x.../0x...
 *
 * Why 2MB stripe alignment is required:
 *   XFS normally allocates blocks at arbitrary offsets. For PMD faults to
 *   succeed, the physical address must be 2MB-aligned. Using mkfs.xfs with
 *   -d su=2m,sw=1 forces XFS to align its allocations to 2MB boundaries.
 *   Without this, PMD faults fall back to PTE faults and the bug is not
 *   exposed.
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: GPL-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

/* 4MB file size ensures we have room for 2MB-aligned PMD mappings */
#define FILE_SIZE (4 * 1024 * 1024)

int main(int argc, char *argv[])
{
	const char *path;
	int fd;
	char *p;
	int rc = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <file_on_dax_mount>\n", argv[0]);
		fprintf(stderr, "\n");
		fprintf(stderr, "Example:\n");
		fprintf(stderr, "  # mkfs.xfs -f -d su=2m,sw=1 /dev/pmem0\n");
		fprintf(stderr, "  # mount -o dax /dev/pmem0 /mnt/pmem\n");
		fprintf(stderr, "  $ %s /mnt/pmem/testfile\n", argv[0]);
		fprintf(stderr, "\n");
		fprintf(stderr, "Or just run: make test\n");
		return 1;
	}

	path = argv[1];

	printf("Creating and opening %s...\n", path);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	printf("Extending file to %d bytes (%d MB)...\n",
	       FILE_SIZE, FILE_SIZE / (1024 * 1024));
	if (ftruncate(fd, FILE_SIZE) < 0) {
		perror("ftruncate");
		rc = 1;
		goto out_close;
	}

	printf("Mapping file with MAP_SHARED...\n");
	p = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		rc = 1;
		goto out_close;
	}

	printf("Mapped at %p\n", p);
	printf("Writing to trigger PMD faults...\n");

	/*
	 * Touch memory across the entire region to trigger page faults.
	 * If the filesystem allocated blocks at 2MB-aligned addresses,
	 * the kernel will use PMD (2MB) mappings. Otherwise, it falls
	 * back to PTE (4KB) mappings.
	 */
	memset(p, 0x42, FILE_SIZE);

	printf("Syncing to persistent memory...\n");
	if (msync(p, FILE_SIZE, MS_SYNC) < 0)
		perror("msync");

	printf("Unmapping (this triggers free_zone_device_folio)...\n");
	if (munmap(p, FILE_SIZE) < 0) {
		perror("munmap");
		rc = 1;
		goto out_close;
	}

	printf("\nDone. Check dmesg for:\n");
	printf("  WARNING: mm/memremap.c:NNN at free_zone_device_folio+0x.../0x...\n");

out_close:
	close(fd);
	unlink(path);
	return rc;
}
