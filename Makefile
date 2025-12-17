CC = gcc
CFLAGS = -Wall -O2

all: dax_pmd_test

dax_pmd_test: dax_pmd_test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f dax_pmd_test

# Quick test target - assumes /dev/pmem0 exists and /mnt/pmem is available
test: dax_pmd_test
	@echo "=== Setting up XFS with 2MB stripe alignment ==="
	sudo umount /mnt/pmem 2>/dev/null || true
	sudo mkfs.xfs -f -d su=2m,sw=1 /dev/pmem0
	sudo mount -o dax /dev/pmem0 /mnt/pmem
	sudo chmod 777 /mnt/pmem
	@echo ""
	@echo "=== Running test ==="
	./dax_pmd_test /mnt/pmem/testfile
	@echo ""
	@echo "=== Checking for warning in dmesg ==="
	@dmesg | tail -5 | grep -q "WARNING.*free_zone_device_folio" && \
		echo "WARNING TRIGGERED - bug confirmed!" || \
		echo "No warning seen (bug may be fixed or PMD faults fell back to PTE)"

.PHONY: all clean test
