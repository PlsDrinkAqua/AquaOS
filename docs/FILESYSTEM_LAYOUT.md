# Filesystem Layout

The installed ZenOS filesystem uses these directories:

| Directory | Contents |
| --- | --- |
| `/bin` | Shell and core commands: cat, ls, echo, mkdir, rm, rmdir, hello, meinkampf |
| `/usr/bin` | Native C compiler, kirkc |
| `/usr/include` | Public C library headers, including sys/ and zenos/ |
| `/usr/lib` | libc.a, crt0.o, and libgcc.a |
| `/usr/lib/tcc` | Compiler support library and license |
| `/home/user` | Example C sources and space for user projects |
| `/tmp` | Temporary files |
| `/etc` | Reserved for system configuration |

Bare command names search `/bin`, then `/usr/bin`. An explicit path such as
`./hello` or `/bin/cat` runs that path directly. The shell and kernel retain a
legacy root-program fallback so older disk images can still boot.

Compile a program with:

```sh
cd /home/user
kirkc hello.c -o hello
./hello
```

`ports/tinycc/build.py` stages this layout, and `ports/tinycc/make_disk.py`
creates a new disk without overwriting an existing one. To migrate an existing
development disk, close QEMU, then run:

```sh
python3 ports/tinycc/build.py
python3 ports/tinycc/migrate_disk.py disk/zenos-dev.img
sh iso.sh
```

Migration works on a copy and checks ext2 integrity before replacing the disk.
The original remains at `disk/zenos-dev.img.before-layout.bak`. It moves legacy
headers, libraries, commands, and bundled example sources; updates installed
system binaries and compiler files; and preserves example edits and unrelated
user files. Destination conflicts abort migration without changing the original.

Host source directories are separate from the installed layout: `user/` contains
program sources, `userlibc/include/` public C headers, `libk/` the kernel C
library, and `kernel/include/` internal kernel headers. Kernel headers are not
installed as user C library headers.
