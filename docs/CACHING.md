# Read buffering and disk page cache

AquaOS provides two separate caching layers. Applications can keep using
`fgetc()`/`fread()`; Kilo does not implement private read-ahead.

## Userspace libc

`fopen`/`fdopen` probe readable descriptors for seekability and lazily allocate a
4 KiB input buffer. Small reads consume that buffer; large `fread()` requests
can bypass it after consuming pending bytes. If allocation fails, the stream
falls back to direct I/O. Standard streams, pipes and sockets remain unbuffered;
output remains synchronous, avoiding pending-output duplication across fork and
loss across exec. This change does not add `setvbuf` or output buffering.

`ftell()` reports the logical position (kernel offset minus unread/pushed bytes).
`fseek(SEEK_CUR)` accounts for read-ahead; successful seeks discard input buffers,
while failed seeks retain them. `ungetc`, EOF, error flags and short reads retain
their stream semantics. Before writing through an update stream, unread input is
rewound and discarded. `fflush(stream)` synchronizes seekable input; `fflush(NULL)`
only concerns output, which is already synchronous. Closing a stream synchronizes
its input position and frees the buffer even if synchronization/close fails.

Libc fork, spawn and exec wrappers synchronize all seekable input streams before
inheriting descriptors. Normal exit synchronizes them before the kernel closes
descriptors. A failed exec leaves the original stream at the same logical position.
These hooks do not make simultaneous use of shared descriptors independent: callers
must still coordinate active handles with `fflush`/positioning. Direct syscalls
bypass libc's lifecycle hooks. Process faults/forced termination cannot run libc
cleanup. External modifications do not magically invalidate private FILE buffers;
seek/flush before reading changes made through other handles.

## Kernel disk page cache

`kernel/kernel/FILESYSTEM/page_cache.c` caches 64 disk pages, each holding 4 KiB
(256 KiB data total plus metadata). The key is the primary ATA device's disk page
index. Per-sector valid masks support 1 KiB ext2 blocks and partial-page requests
without reading beyond requested sectors. Hits copy from RAM; misses read the
requested sectors with uncached PIO. Least recently used pages are evicted.

Every normal primary ATA read/write goes through the cache, including ext2 data,
inodes, allocation bitmaps, group descriptors and superblock updates. Successful
writes go to the device first, then update overlapping cached sectors. Short or
failed writes invalidate all affected cached sectors. Failed reads are not cached.
This keeps separate processes, inode updates, truncation and block reuse coherent
at the kernel layer. There is no delayed writeback or additional power-loss
durability guarantee; the underlying ATA driver retains its existing polling and
error-detection limitations. Device reset discards cached pages.

PIO/cache operations disable task preemption on the existing single-CPU kernel;
there are no filesystem operations in IRQ context. This is a disk page cache,
not yet an inode-indexed Linux-style file page cache shared with mmap. There is no
SMP locking, memory-pressure reclaim, hotplug or support for multiple disk devices.
The bounded static cache is reserved with the rest of the kernel at boot.

`disk_cache_stats` records hit/miss counts, backend sectors read/written and LRU
evictions. The Kilo profile harness reads this kernel symbol through QEMU to
report per-run deltas; it is not a userspace ABI.

## Checks

```sh
python3 tests/stdio_host.py
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I kernel/include \
  -idirafter libk/include tests/page_cache_host.c -o /tmp/aquaos-cachecheck
/tmp/aquaos-cachecheck
sh build.sh
python3 ports/tinycc/build.py --arch x86_64
python3 tools/iso64.py
python3 tests/shell_smoke.py --block-size 4096 --tinycc
python3 tests/shell_smoke.py --block-size 1024 --kilo-only
```

The guest stdio check covers buffered positioning, read/write transitions, fork,
normal child exit, successful/failed exec and cross-process truncate/read
consistency. Host tests cover buffering fallback, short/error reads, nonseekable
streams, cache hits, partial writes, sector boundaries, count-zero ATA requests,
LRU eviction and reset. The Kilo regressions retain editing/save and offline ext2
integrity checks. See [Kilo profiling](../ports/kilo/README.md#system-level-read-performance).

Rebuild the TinyCC installation tree after rebuilding libc so programs compiled
inside the guest link the new library. Existing guest binaries are statically
linked and need rebuilding/reinstalling; rebuilding the host tree does not modify
existing guest disks.
