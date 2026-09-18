# TinyCC on ZenOS

This port builds TinyCC 0.9.27 (`d348a9a51d32cece842b7885d27a411436d7887b`)
with the existing i686-elf toolchain. The resulting `/usr/bin/kirkc` runs on ZenOS and
compiles/links static i386 ELF executables using ZenOS startup code and libc.
It does not use Linux syscalls. The first build downloads the pinned archive;
subsequent builds use its SHA-256-verified cache.

For normal development, run `sh qemu.sh`. It creates the TinyCC development
disk on the first launch, rebuilds the kernel ISO, and boots that disk. Subsequent
launches preserve the disk and your source files. Existing installed user programs
and compiler files are not automatically refreshed when their host sources change.
Extra QEMU arguments can be passed through, for example `sh qemu.sh -s -S`.

To build and test each step explicitly, from the repository root:

```sh
python3 ports/tinycc/build.py
make -C user
sh iso.sh
python3 tests/shell_smoke.py --block-size 4096 --tinycc
python3 ports/tinycc/make_disk.py
qemu-system-i386 -m 128M -cdrom myos.iso \
    -drive file=disk/zenos-dev.img,format=raw,if=ide
```

The disk creator needs `mkfs.ext2`, `debugfs`, and `e2fsck`, but no root or loop
mount. It creates a separate 100 MiB development disk and refuses to overwrite
an existing path. Supply an alternate output filename as its argument to make
another disk. Existing disk images are not updated by the build or smoke test.
To move an older development disk to the organized layout, close QEMU and run
`python3 ports/tinycc/migrate_disk.py`. It preserves the original disk as a
backup; see [Filesystem Layout](../../docs/FILESYSTEM_LAYOUT.md).
The host build needs Python 3.12+, the cross compiler, and make; boot/testing
also needs QEMU and the usual GRUB ISO build dependencies. `ZENOS_CC` and
`ZENOS_AR` can override the cross compiler and archiver commands.

Inside ZenOS:

```sh
cd /home/user
kirkc hello.c -o hello
./hello
meinkampf hello.c
kirkc hello.c -o hello
./hello
```

`hello.c` is installed in `/home/user` on the development disk. The expected greeting is
`Hello from native TinyCC!`. Sources can include headers in `/usr/include`; libraries
are in `/usr/lib` and `/usr/lib/tcc`. `kirkc -c` produces objects, and multiple source/object
inputs can be linked. `-nostdlib` retains its normal opt-out meaning.

## Integration

`build.py` applies explicit, checked substitutions to a fresh pinned source
extraction. A mismatched patch fails the build. The patched sources remain in
`build/` for inspection. Output in `root/` contains `/usr/bin/kirkc`, ZenOS headers,
TinyCC's stdarg/stdbool/float headers, `crt0.o`, `libc.a`, `libtcc1.a`, the matching
cross-toolchain `libgcc.a`, and example/regression sources. TinyCC's license is
installed at `/usr/lib/tcc/COPYING`; upstream sources retain their own notices.

The compiler links `crt0.o` automatically and defaults to static linking. Its
support runtime handles compiler-generated operations; libgcc supplies helpers
used by GCC-built libc. `__zenos__` is predefined, with no Linux target macro.
ZenOS libc supplies i386 setjmp/longjmp; jumping with zero returns one, and
callee-saved registers and the caller stack are restored. Ordinary C rules for
volatile locals and the lifetime of the target stack frame apply. This is not
signal-mask-saving sigsetjmp.

## Current limits

- The first target is compile-to-file, not `-run`, dynamic libraries, bounds
  checking, or backtraces. Shared/memory output and cross-target
  dispatch are rejected. Native compiler self-rebuilding is tested below.
- There is no environment or wall-clock ABI. Compiler search paths are explicit;
  environment lookups are absent. `__DATE__` expands to `Jan  1 1970` and
  `__TIME__` to `00:00:00`. `-bench` is rejected.
- Use 4 KiB ext2 blocks: runtime archives exceed the 268 KiB file capacity of
  1 KiB images. Single-indirect file limits still apply (4,243,456 bytes at 4 KiB).
- This uses the existing 256 KiB user stack and 64 MiB managed-memory ceiling.
  Large projects and deeply nested inputs have not been characterized.
- Libc remains a subset: no wide I/O, full math library, locale/environment API,
  or complete POSIX compatibility. The math classification macros currently
  depend on GCC builtins; use the implemented math functions where applicable
  in TinyCC-generated programs. Header availability is not full C conformance.
- Failed ELF writes/closes report failure and remove the partial output.
  Output replacement is not transactional; do not use a source file as `-o`.

The native smoke test compiles and runs a greeting, multi-source and object-file
builds, varargs, integer/float/long-double operations, string scanning, and
nonlocal jumps. Bad syntax, missing headers, and an unwritable output path must
fail; another compilation must then succeed. Existing shell, filesystem,
allocator and stdio regressions run afterward, with offline ext2 validation.

## Rebuilding TinyCC inside ZenOS

The staged tree now includes the exact patched compiler sources and generated
`config.h` at `/home/user/tinycc`, plus the existing utility source `echo.c`
at `/home/user/echo.c`. The source directory contains upstream top-level C,
header and definition files and COPYING; it is sufficient for the configured
i386 single-translation-unit build, not an upstream build-system installation.
The host and native builds use the same patched inputs.

Inside ZenOS, the complete compiler rebuild and test is now automated:

```sh
cd /home/user/tinycc
shell build.sh
```

The script stops on failure and prints `native build complete` on success. See
[Shell scripts](../../docs/SHELL.md) for script semantics. The individual steps
can also be run manually:

```sh
cd /home/user
kirkc echo.c -o native-echo
./native-echo native utility works
cd tinycc
kirkc tcc.c -o stage2
./stage2 tcc.c -o stage3
./stage3 ../hello.c -o greeting
./greeting
./stage3 ../nativecheck.c ../companion.c -o check
./check
```

The original `/usr/bin/kirkc` remains installed. The native commands need no
extra defines or include paths: `__zenos__` is predefined by kirkc, `config.h`
selects the static i386 target, and compiler headers/libraries use their
installed paths. Each resulting compiler still uses the installed host-built
libc, crt0 and runtime archives. This milestone does not rebuild those libraries
or the kernel.

To reproduce the full test on a disposable disk from the host:

```sh
python3 ports/tinycc/build.py
make -C user
sh iso.sh
python3 tests/shell_smoke.py --block-size 4096 --selfhost
```

`--selfhost` implies `--tinycc`. It builds and runs the existing echo utility,
runs `shell build.sh` to build stage 2 with the installed compiler and stage 3
with stage 2, then tests
both generated compilers with greetings, object-file linking, native ABI checks,
and recovery after syntax, missing-header and output-open failures. After QEMU
exits, it checks filesystem integrity and compares the two compiler executables
byte for byte. Add `--layout-only` for a shorter self-hosting and shell-layout
check that skips the broader kernel/libc regression suite.

Existing development disks are not refreshed by the build. For a fresh disk
containing the sources, run `python3 ports/tinycc/make_disk.py disk/zenos-selfhost.img`
and boot it with the QEMU command printed by that script.
