# ZenOS

ZenOS is an x86-64 operating system with LP64 userspace, a shell, a text editor,
a writable ext2 filesystem, and a native TinyCC compiler. x86-64 is the only
supported architecture.

```sh
sh qemu.sh      # build and boot a fresh development disk
sh build.sh     # build the kernel, libc, and user programs
sh iso.sh       # build and package the boot ISO
make test       # build and run the QEMU regression suite with TinyCC
```

Build artifacts live in `build/x86_64`. Development disks remain under `disk/`.
`sh clean.sh` removes build artifacts without removing guest disks.

See [build requirements and architecture](docs/X86_64.md),
[TinyCC](ports/tinycc/README.md), and [shell features](docs/SHELL.md).

`kilo <file>` opens the lightweight editor with C/C++ syntax highlighting and
search. Ctrl+S saves, Ctrl+F searches, and Ctrl+Q quits. See the
[Kilo port](ports/kilo/README.md) for controls and verification.
