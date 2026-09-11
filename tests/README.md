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
Bare cat copies standard input; cat accepts multiple paths and '-' for stdin.
Commands without a slash are looked up in the root directory; paths containing
a slash are used as written.

Each process has a working directory, initially /. chdir changes it and
getcwd copies its absolute path into a caller-provided buffer. fork and spawn
inherit it; exec preserves it. The shell's cd and pwd run in the shell process.
Relative file paths and executable paths resolve against that directory.
Paths are limited to 255 bytes after prefixing the working directory.
