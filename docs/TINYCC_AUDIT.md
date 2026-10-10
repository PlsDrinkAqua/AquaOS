# TinyCC integration

ZenOS builds pinned TinyCC 0.9.27 revision
`d348a9a51d32cece842b7885d27a411436d7887b` for native x86-64 static ELF64 output.
The port uses ZenOS libc, startup code, syscall ABI, and x86-64 runtime helpers.
It supports compilation to files and native compiler self-rebuilding.

`ports/tinycc/build.py` verifies the source archive hash and applies checked
patches to the pinned source. Patches configure static linking, ZenOS compiler
paths and macros, libc startup/runtime selection, static PLT32/GOT relocation,
and output-error cleanup. Timing, cross-target dispatch, shared libraries, and
in-memory execution remain disabled.

Validation covers generated code, varargs, floating point, setjmp/longjmp,
multi-source/object builds, errors and recovery, and two native compiler rebuilds.
See [TinyCC instructions](../ports/tinycc/README.md) and
[x86-64 architecture](X86_64.md) for current commands and ABI limits.
