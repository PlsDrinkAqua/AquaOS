# TinyCC dependency audit for ZenOS

Implementation update: ZenOS now provides FILE streams (unbuffered), integer,
string and floating-point printf formatting, sscanf/vsscanf, strtof/strtod/
strtold, ldexp and supporting x87 math helpers, ASCII ctype, fcntl access/append
handling, and variadic open source compatibility. The earlier realloc,
memory/string routines, integer conversion and qsort batch is also implemented.
The kernel now provides a 256 KiB user stack with a guard page and per-task
x87/SSE initialization, context switching, fork inheritance and exec reset.
Host and QEMU tests exercise these additions; see tests/README.md for the
supported subsets and validation commands.

The tables and platform findings below preserve the original audit snapshot;
the additions above are no longer missing. setjmp/longjmp, time/environment
adaptations, compiler-independent headers, TinyCC configuration/build work and
end-to-end compilation on ZenOS remain. These library tests do not establish
that TinyCC itself is ported.

Baseline: TinyCC `release_0_9_27`, commit
`d348a9a51d32cece842b7885d27a411436d7887b`.
This is a pinned release audit, not a claim that this is the latest development
version. ZenOS source was inspected on 2026-09-13. No OS implementation was
changed for this audit.

Sources:
- https://github.com/TinyCC/tinycc/tree/d348a9a51d32cece842b7885d27a411436d7887b
- https://bellard.org/tcc/tcc-doc.html

## Initial port scope

A compiler that runs on ZenOS/i386 and writes static ELF32 ET_EXEC programs
using ZenOS libc, startup code and syscalls. Build the compiler and its runtime
on the Linux host first. Native self-rebuilding, shared libraries, `-run`,
bounds checking, runtime backtraces and switching to another architecture are
separate milestones.

## What already exists

- ELF32 little-endian EM_386 ET_EXEC loader, user address spaces and exec(argv).
- i386 startup code `userlibc/crt0.S` calling main(argc, argv), then exit.
- malloc/free/calloc backed by sbrk, with overflow checks and zeroed new pages.
- read/write/open/close/lseek, directories, working directories, unlink and
  limited same-directory rename. Pipes and fork/wait already exist.
- errno, printf/puts/putchar/getchar, strlen/strcmp/strncmp.
- Persistent ext2 storage and an editor for small source files.

These are useful foundations, not proof of compiler compatibility.

## Required libc and header work

| Area | Missing or incomplete | Source evidence / consequence |
| --- | --- | --- |
| Memory | realloc, memcpy, memmove, memset, memcmp | libtcc.c allocation wrappers and compiler section/token buffers; implement in userlibc, not only libk |
| Strings | strcpy, strchr, strrchr, strstr | Paths, arguments and linker scripts; existing strlen/strcmp/strncmp are available |
| Integer conversion | atoi, strtol, strtoul, strtoll, strtoull | CLI options, numeric preprocessing and archive sizes |
| Formatting | snprintf, vsnprintf, sprintf, fprintf, sscanf | Diagnostics, version parsing, output paths, archive tools; current printf lacks width/precision/length handling |
| Streams | FILE, stdout/stderr, fopen, fdopen, fclose, fflush, fread, fwrite, fseek, ftell, fputc, fputs | Compiler uses both descriptor I/O and stdio; `tcc_write_elf_file` uses fdopen and fwrite |
| Error recovery | jmp_buf, setjmp, longjmp | libtcc.c uses them to recover from compiler errors; not an optional debug feature |
| Sorting | qsort | tccgen.c sorts switch cases |
| Floating point | strtof, strtod, strtold, ldexp | tccpp.c parses floating literals, including hexadecimal floats |
| Header/API compatibility | fcntl.h, setjmp.h, math.h, time.h, sys/time.h and fuller existing headers | Explicit tcc.h includes; provide APIs or deliberately remove the corresponding feature |
| Standard types | Supply/install appropriate stdarg.h, stdint.h, limits/float definitions and a complete enough stddef.h | Host GCC currently supplies some headers; a native compiler cannot depend on the host cross-compiler installation |

Stream wrappers can use existing syscalls. Implement correct short I/O, stream
positioning and error handling. TCC's output path does not robustly propagate
every fwrite/fclose error; audit that path so disk-full cannot report success
with a corrupt executable. A complete scanf or printf implementation can be
staged, but do not silently ignore the formats TCC actually uses.

ZenOS declares open(path, flags) with two arguments. TCC calls open with a third
creation-mode argument. Either provide a compatible variadic libc wrapper or
patch those calls. Kernel support for arbitrary file modes is a separate choice.
The different exec/wait signatures are not blockers for compile-to-file mode.

## Features that can be adapted or deferred

| Dependency | Why it appears | Initial-port choice |
| --- | --- | --- |
| getenv | C_INCLUDE_PATH, CPATH, LIBRARY_PATH, LD_SO | No environment ABI exists; explicitly use configured paths and return NULL for absent variables, or remove environment lookup |
| time/localtime | __DATE__ and __TIME__ preprocessing | Implement clock support or document a deterministic fallback; do not dereference a fake NULL tm |
| gettimeofday | Compilation timing / -bench | Remove timing initially or adapt to a clock syscall |
| remove | Temporary archive cleanup | File-only wrapper around unlink, or defer the -ar tool |
| execvp | -m32/-m64 dispatch to a different compiler executable | Disable cross-architecture dispatch; not needed for an i386-only compiler |
| mmap/mprotect, dlopen/dlsym/dlclose, signal/ucontext | Native -run, dynamic loading, backtraces / bounds checks | Disable the corresponding code paths for the first port |

Important: in this release, tcc.h automatically defines TCC_IS_NATIVE when the
build CPU matches the target and then enables backtrace/bounds code. A Linux
x86-64 build targeting i386 naturally omits those paths, whereas an actual
ZenOS i386 build will enable them unless patched. CONFIG_TCC_STATIC alone does
not remove native execution. The guessed configure switches --disable-bcheck
and --disable-backtrace are not recognized by this release; feature gating
needs a deliberate source/configuration change.

No stat/fstat/access dependency appeared in the inspected Linux/i386
compile-to-file CLI object or its core source paths. These APIs are useful
future additions but are not established first-port requirements here.

## Kernel and runtime blockers

1. **User stack:** task_prepare_user_stack maps one 4096-byte page. TinyCC has
   recursive parsing and multiple large local buffers. tcctools.c alone has
   `char program[4096]`; measured host compiler frames also reach 2–2.5 KiB in
   parsing/assembler routines. Increase the mapped user stack (for example,
   start with 256 KiB plus an unmapped guard page), then measure on i386. The
   suggested size is an engineering starting point, not a verified minimum.
2. **FPU state:** no explicit x87 initialization or per-task FPU save/restore
   was found in the kernel. TinyCC performs floating-point work while compiling,
   and generated i386 floating-point code also needs it. Add proper initialization,
   context management and fork/exec semantics, or use a fully considered software
   floating-point strategy. An integer-only demonstration is not a full C port.
3. **File capacity:** ext2 currently traverses 12 direct blocks plus one
   single-indirect block. Capacity is `(12 + block_size / 4) * block_size`:
   268 KiB at 1 KiB blocks, about 4.05 MiB at 4 KiB blocks. Measure stripped
   compiler/runtime/archive sizes. Larger-block images may suffice initially;
   double-indirect support is not automatically mandatory for a tiny demo.
4. **Physical memory:** PMM manages at most 64 MiB, even with 128 MiB configured
   in QEMU. Profile compiler peak memory, including kernel-side ELF loading and
   fork copies. No measured TinyCC-on-ZenOS RAM minimum is available yet.
5. **Limits to test:** 16 descriptors/process, 16 tasks, 255-byte resolved paths,
   and exec limited to 32 arguments / 1024 argument-string bytes. These may work
   for hello.c but are not a promise of large-project compatibility.

## Compiler target integration and installed files

- Build TCC itself with the existing i686-elf toolchain against ZenOS headers,
  libc and crt0; a Linux i386 binary will not use ZenOS's syscall ABI.
- Set target include/library/runtime paths to locations on the OS disk.
- Match the i386 C ABI (including long double, alignment and variadic calls).
- Replace default crt1.o/crti.o/crtn.o expectations with ZenOS startup behavior.
  Initially, explicit -nostdlib plus crt0.o, libc.a and required runtime objects
  can establish the executable format before making the CLI defaults convenient.
- Build/install i386 libtcc1.a and required alloca helpers. TCC's emitted code
  may call arithmetic/conversion helpers; do not assume host libgcc is a
  drop-in replacement. The GCC-built compiler itself may also need libgcc.
- Generate static ET_EXEC with valid PT_LOAD segments and _start. ZenOS already
  has this loader shape; validate the actual generated layout in QEMU. Shared
  objects, a Linux ELF interpreter and dynamic linking are outside initial scope.
- Define an OS macro such as __zenos__ and avoid claiming Linux compatibility.
- Install TCC's supplied headers alongside the expanded ZenOS headers; host
  sysroot files are not automatically installed into the ext2 image.

## Suggested implementation order and acceptance checks

1. Memory/string helpers, realloc, integer conversion and qsort; test behavior
   and allocation failures.
2. Formatting/streams and open compatibility; test partial writes, disk-full,
   diagnostics and include/object-file reading.
3. setjmp/longjmp, floating conversion/math, larger stack and FPU lifecycle;
   test compiler error recovery and preemption between floating-point users.
4. Port build configuration, feature gates, startup and runtime installation.
5. In QEMU: compile/run an integer program; then puts, includes, multiple source
   files, 64-bit arithmetic, floating constants/operations, bad syntax, missing
   headers, repeated compilations and failed output writes.

Only after these pass should rebuilding TCC inside ZenOS be the milestone.

## Audit method and representative imports

Source inspected: tcc.h, tcc.c, tcctools.c, libtcc.c, tccpp.c, tccgen.c,
tccasm.c, i386-gen.c, tccelf.c, tccrun.c and lib/libtcc1.c. Built a host-executable
compiler targeting i386 (`make cross-i386`) and its runtime archive in /tmp.
Also compiled a single object with:

```sh
gcc -std=gnu99 -O2 -fno-builtin -fno-stack-protector \
  -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fstack-usage \
  -DTCC_TARGET_I386 -I. -c tcc.c -o audit.o
nm -u audit.o
```

This is a representative dependency inventory, not a completed ZenOS build.
The compiler runs on host x86-64 and emits i386 code. Some inline libc functions
(e.g. atoi) resolve to other symbols. `__isoc23_*`, `__errno_location` and
`_setjmp` below are host libc spellings, not new ZenOS syscall requirements.
Host stack-usage numbers are diagnostic evidence, not exact i386 frame sizes.

```
                 U __errno_location
                 U __isoc23_sscanf
                 U __isoc23_strtol
                 U __isoc23_strtoll
                 U __isoc23_strtoul
                 U __isoc23_strtoull
                 U _setjmp
                 U close
                 U execvp
                 U exit
                 U fclose
                 U fdopen
                 U fflush
                 U fopen
                 U fprintf
                 U fputc
                 U fputs
                 U fread
                 U free
                 U fseek
                 U ftell
                 U fwrite
                 U getcwd
                 U getenv
                 U gettimeofday
                 U ldexp
                 U localtime
                 U longjmp
                 U lseek
                 U malloc
                 U memcmp
                 U memcpy
                 U memmove
                 U memset
                 U open
                 U printf
                 U qsort
                 U read
                 U realloc
                 U remove
                 U snprintf
                 U sprintf
                 U stderr
                 U stdout
                 U strchr
                 U strcmp
                 U strcpy
                 U strlen
                 U strncmp
                 U strrchr
                 U strstr
                 U strtod
                 U strtof
                 U strtold
                 U time
                 U unlink
                 U vsnprintf
```
