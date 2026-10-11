# Filesystem Layout

The installed ZenOS filesystem uses these directories:

| Directory | Contents |
| --- | --- |
| `/bin` | Shell and core commands: cat, ls, echo, mkdir, rm, rmdir, hello, kilo |
| `/usr/bin` | Native C compiler, tcc |
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
tcc hello.c -o hello
./hello
```

`ports/tinycc/build.py` stages this layout, and `ports/tinycc/make_disk.py`
creates a new x86-64 disk without overwriting an existing one.
Legacy disk migration is no longer supported.

Host source directories are separate from the installed layout: `user/` contains
program sources, `userlibc/include/` public C headers, `libk/` the kernel C
library, and `kernel/include/` internal kernel headers. Kernel headers are not
installed as user C library headers.
