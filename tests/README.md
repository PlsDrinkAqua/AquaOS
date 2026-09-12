Build and run the QEMU smoke checks from the repository root:

```sh
make -C user
sh iso.sh
python3 tests/shell_smoke.py
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

The shell runs standalone /cat, /ls, and /echo, with space/tab-separated
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
