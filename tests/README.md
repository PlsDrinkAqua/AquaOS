Build and run the QEMU smoke checks from the repository root:

```sh
make -C user
sh iso.sh
python3 tests/shell_smoke.py
python3 tests/shell_smoke.py --block-size 4096
```

The test creates and removes its own ext2 disk. It does not modify your existing
disk images. It requires the cross compiler, GRUB build tools, QEMU, and
`mkfs.ext2`/`debugfs`.

User programs now link `userlibc/crt0.o` and implement `int main(void)` or
`int main(int argc, char **argv)`. Returning from main calls exit with that status.
The initial i386 stack contains argc, argv pointers, and a NULL terminator.
The startup routine aligns the stack before calling C.

`exec(path, argv)` accepts up to 32 arguments and 1024 total bytes of argument
strings, including terminators. argv must end with NULL. A NULL argv supplies
the executable path as argv[0]; an empty vector produces argc = 0.
Invalid pointers return EFAULT; excessive arguments return E2BIG.
All user programs must be rebuilt for the new exec syscall ABI.

The shell runs standalone /cat, /ls, /echo, /mkdir, /rm, and /rmdir, with space/tab-separated
arguments. It supports up to four pipeline stages and <, >, and >> redirection.
Quoting, escapes, and environment variables are not implemented.
Bare cat copies standard input; press Ctrl+D to finish keyboard input.
cat accepts multiple paths and '-' for stdin. Nested shells return to their
parent with `exit` or Ctrl+D at an empty prompt.
Commands without a slash are looked up in the root directory; paths containing
a slash are used as written.

Each process has a working directory, initially /. chdir changes it and
getcwd copies its absolute path into a caller-provided buffer. fork and spawn
inherit it; exec preserves it. The shell's cd and pwd run in the shell process.
Relative file paths and executable paths resolve against that directory.
Paths are limited to 255 bytes after prefixing the working directory.

Directory creation is available through `int mkdir(const char *path)` in
`<unistd.h>` and the standalone `/mkdir` command:

```sh
mkdir /projects
cd /projects
mkdir first second
cd first
echo hello > note.txt
ls
```

This is a ZenOS one-argument API: new directories use mode 0755. The command
accepts path arguments only; option parsing and recursive parent creation
(`-p`) are not implemented. Parents must exist. Existing files/directories
return EEXIST, missing parents return ENOENT, and non-directory parents return
ENOTDIR. Directory creation writes real ext2 inodes, '.' and '..' entries,
parent links, and allocation/directory counters. These metadata writes are
not a journaled transaction.

The smoke test exercises directory creation through the shell, then shuts down
QEMU and runs read-only `e2fsck -fn`
on the disposable disk. It also reads back a file inside a newly created
directory with debugfs to verify persistence. `e2fsck` is therefore required
as well.

File deletion is available through `unlink(path)` and `/rm`; empty directories
can be removed through `rmdir(path)` and `/rmdir`. Both commands accept multiple
paths and return a nonzero status if any removal fails. Options and recursive
deletion are not implemented. For example:

```sh
mkdir /scratch
echo temporary > /scratch/note
rm /scratch/note
rmdir /scratch
```

Unlink removes a name immediately, decrements its inode's link count, and frees
the inode and data blocks only after the last link and open descriptor are gone.
Separate opens, dup, fork, and process exit all participate in this lifetime.
Open descriptors refresh inode metadata before reading, writing, seeking, or
listing, so stale handles cannot restore a deleted link or use freed blocks
after another descriptor truncates the file.

Unlink rejects directories with EISDIR. Rmdir rejects regular files with
ENOTDIR, nonempty directories with ENOTEMPTY, and root, open directories, or
any live process's working directory with EBUSY. Final `.` and `..` components
are rejected with EINVAL. A trailing slash requires a directory. Deletion of
special files (including symlinks), double/triple-indirect layouts, and inodes
with external attribute blocks is currently rejected before changing metadata.
Direct and single-indirect blocks, including sparse files, are reclaimed.

Like mkdir, deletion is not journaled. A crash or disk error during metadata
updates can require e2fsck; there is no persistent orphan recovery for files
unlinked while open. A final-close reclamation error is returned by close;
process exit has no caller to receive that error.

The smoke runner builds a test-only `/fscheck` from `tests/fs_delete.c` and
installs it on the disposable disk. It checks syscall errors, hard links,
independent and duplicated descriptors, fork/exit cleanup, another process's
working directory, large and sparse file reclamation, and deletion across
directory block boundaries. Shell commands run afterward, also checking that
kernel heap mappings remain usable when the stress program exits. Offline
e2fsck and debugfs checks validate disk consistency, retained contents, and
the absence of deleted paths. Both 1 KiB and 4 KiB ext2 blocks are supported
by this test setup.

`make -C user` builds the new commands, and `disk_test.sh` includes them when
creating a fresh disk. The smoke runner does not update existing disk images.

Memory reliability checks:

```sh
sh tests/allocator_host.sh
make -C user
sh iso.sh
python3 tests/shell_smoke.py
```

The host test compiles the real kernel and user allocators against bounded
backing storage. It checks overflow rejection, adjacent-block reuse without
heap growth, live-allocation contents, exhaustion/recovery, and preservation of
memory reserved directly through sbrk. It requires a host C compiler (`cc`).
The native host test supplements the i386 QEMU tests; it does not emulate
paging or scheduling.

The smoke runner also builds and runs the test-only `/memcheck`. It verifies
8-byte malloc alignment, allocation overflow, repeated allocation/free cycles,
sbrk bounds (including INTPTR_MIN), zero-filled newly mapped heap pages, and
rollback after requests exceed available physical memory. Forty fork/wait
cycles include intentional user protection faults and repeated descriptor
exhaustion. The parent and shell must remain usable afterward. User exceptions
in this test are expected; a `FAIL` marker or fatal kernel exception fails the
smoke runner immediately.

The kernel allocator now coalesces adjacent free ranges and serializes its
free list against task preemption. User malloc aligns both metadata and
returned pointers, checks sizes before rounding or calling sbrk, and does not
merge across allocations owned by direct sbrk callers. Applications must still
free valid allocation pointers exactly once and must not shrink sbrk across
live malloc allocations. Freed kernel heap memory is retained for reuse;
returning unused heap pages to the physical allocator and kernel stack guards
remain future work.
