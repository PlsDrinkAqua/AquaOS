# Large files

AquaOS uses signed 64-bit `off_t`, `lseek` syscall arguments/results and kernel
seek APIs. Descriptor positions are 64-bit, including shared positions after
`dup` and `fork`. `fseek`/`ftell` use 64-bit `long` on the native LP64 ABI.
Seek addition rejects overflow and negative positions without changing the
position. Pushback adjustment rejects underflow before subtracting.

This changes the libc ABI: rebuild programs and native compiler archives.
`sh qemu.sh` builds a fresh development image with the updated binaries and
headers. Old disk images keep their old binaries until explicitly updated.

Ext2 regular-file reads, writes, truncation, unlink and replacement traverse all
three indirect levels. Missing blocks read as zeros. Seeking past EOF does not
allocate storage; writing allocates only the required data and pointer blocks.
The high inode-size word is persisted, and revision-1 LARGE_FILE is enabled
before writing a file larger than 2 GiB. The block formats follow the
[Linux ext2 documentation](https://cdn.kernel.org/doc/html/latest/filesystems/ext2.html).

Limits remain finite: the block tree holds `12 + N + N² + N³` blocks, where
`N = block_size / 4`. Allocated sectors (including pointer blocks) must fit the
inode's 32-bit `i_blocks` field. The ATA driver's device addressing remains a
separate volume limit. A 64-bit seek does not imply support for arbitrary disk
sizes. External inode attribute blocks and special-file deletion remain
unsupported. Filesystem metadata updates are not journaled; crashes or I/O
errors during changes can still require recovery with e2fsck.

New indirect subtrees are built before their root pointer is published.
Allocation exhaustion or a failed construction write frees the unpublished
blocks. Successful write chunks persist the updated inode before proceeding,
so a later allocation failure can return a short transfer with earlier data
and size retained. This is not a transactional power-loss guarantee.

## Validation

```sh
cc -Wall -Wextra -ffunction-sections -fdata-sections \
  -fsanitize=address,undefined -Ikernel/include -idirafter libk/include \
  tests/large_file_host.c -Wl,--gc-sections -o /tmp/aquaos-large-host
/tmp/aquaos-large-host
python3 tests/stdio_host.py
python3 tools/build64.py
python3 tools/iso64.py
AQUAOS_TEST_LARGE_FILE=1 python3 tests/network_smoke.py
AQUAOS_TEST_LARGE_FILE=1 AQUAOS_TEST_BLOCK_SIZE=1024 python3 tests/network_smoke.py
python3 tests/shell_smoke.py --block-size 4096
```

Host tests check all block-tree boundaries, reclamation and unpublished-tree
rollback under allocation/write failures for 1 KiB and 4 KiB blocks. Stdio host
tests seek beyond 5 GiB. The QEMU large-file mode creates a disposable filesystem
with LARGE_FILE initially disabled, writes and reads sparse data across indirect
boundaries and above 5 GiB, checks shared descriptor positions and seek overflow,
and exercises stdio, truncation and unlink-while-open. After each NIC layout,
read-only e2fsck checks integrity and debugfs verifies the retained 5 GiB + 1
byte file size. Existing development disks are untouched.
