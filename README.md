# FS-DAX Large Folio Warning Reproducer

This repository contains a test program that reproduces a spurious kernel
warning in `mm/memremap.c:free_zone_device_folio()` when FS-DAX uses PMD
(2MB) mappings.

## The Bug

The `VM_WARN_ON_ONCE(folio_test_large(folio))` check incorrectly triggers
for `MEMORY_DEVICE_FS_DAX` when PMD mappings are used. FS-DAX legitimately
creates large file-backed folios for PMD faults - this is a core performance
feature. The warning was introduced by a commit that added support for large
zone device private folios but did not account for FS-DAX.

## Prerequisites

- Linux kernel with FS-DAX support and `CONFIG_DEBUG_VM=y`
- A pmem device (real or emulated via `memmap` kernel parameter)
- An fsdax namespace configured via ndctl
- XFS tools (`mkfs.xfs`)

## Quick Start

```bash
git clone <REPO_URL>
cd xfs-dax-test
make
sudo make test
```

The `make test` target will:
1. Create an XFS filesystem with 2MB stripe alignment on `/dev/pmem0`
2. Mount it with DAX at `/mnt/pmem`
3. Run the test program
4. Check dmesg for the warning

## Manual Steps

If you need to customize the device or mount point:

```bash
# 1. Create fsdax namespace (if needed)
sudo ndctl create-namespace -m fsdax -e namespace0.0

# 2. Create XFS with 2MB stripe alignment (CRITICAL!)
sudo mkfs.xfs -f -d su=2m,sw=1 /dev/pmem0
sudo mount -o dax /dev/pmem0 /mnt/pmem

# 3. Build and run the test
make
./dax_pmd_test /mnt/pmem/testfile

# 4. Check for warning
dmesg | grep "WARNING.*free_zone_device_folio"
```

## Why 2MB Stripe Alignment?

XFS normally allocates blocks at arbitrary offsets. For PMD faults to succeed,
the physical address must be 2MB-aligned. Without the `-d su=2m,sw=1` option,
XFS allocations are not aligned, causing PMD faults to fall back to PTE (4KB)
faults - which hides the bug.

The stripe alignment option forces XFS to align data allocations to 2MB
boundaries, allowing PMD faults to succeed and exposing the warning.

## Expected Output

On an unpatched kernel with `CONFIG_DEBUG_VM=y`:

```
WARNING: mm/memremap.c:431 at free_zone_device_folio+0x.../0x...
```

## The Fix

The fix is to exempt `MEMORY_DEVICE_FS_DAX` from the large folio warning:

```diff
--- a/mm/memremap.c
+++ b/mm/memremap.c
@@ -427,7 +427,12 @@ void free_zone_device_folio(struct folio *folio)
 	if (folio_test_anon(folio)) {
 		for (i = 0; i < nr; i++)
 			__ClearPageAnonExclusive(folio_page(folio, i));
 	} else {
-		VM_WARN_ON_ONCE(folio_test_large(folio));
+		/*
+		 * FS-DAX legitimately uses large file-backed folios for PMD
+		 * mappings, so only warn for other device types.
+		 */
+		VM_WARN_ON_ONCE(pgmap->type != MEMORY_DEVICE_FS_DAX &&
+				folio_test_large(folio));
 	}
```

## License

SPDX-License-Identifier: GPL-2.0
