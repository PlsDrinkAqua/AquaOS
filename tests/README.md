For the native x86-64 build, see [X86-64](../docs/X86_64.md):

```sh
python3 tools/build64.py
python3 ports/tinycc/build.py --arch x86_64
python3 tools/iso64.py
python3 tests/shell_smoke.py --arch x86_64 --block-size 4096 --selfhost
```

The commands below describe the retained **i386** build. The test runner defaults
to i386 unless `--arch x86_64` is supplied.

Build and run the QEMU smoke checks from the repository root:

```sh
make -C user
sh iso.sh
python3 tests/shell_smoke.py
python3 tests/shell_smoke.py --block-size 4096
python3 ports/tinycc/build.py
python3 tests/shell_smoke.py --block-size 4096 --tinycc --layout-only
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

The shell runs standalone commands in `/bin`, with space/tab-separated
arguments. It supports up to four pipeline stages and <, >, and >> redirection.
Quoting, escapes, and environment variables are not implemented.
At the prompt, Tab completes the final filename or path in the current directory
or the directory prefix you typed. A unique directory gains a trailing `/`;
multiple matches extend only their common prefix, and no match leaves input
unchanged. Hidden names require a leading dot. Completion works in arguments
and after redirection operators as well as at the start of the line; it does
not search command installation directories. Names containing whitespace or
shell operators are skipped because the shell cannot quote them yet.
Bare cat copies standard input; press Ctrl+D to finish keyboard input.
cat accepts multiple paths and '-' for stdin. Nested shells return to their
parent with `exit` or Ctrl+D at an empty prompt.
Commands without a slash search `/bin`, `/usr/bin`, then the legacy root
directory; paths containing a slash are used as written. See
[Filesystem Layout](../docs/FILESYSTEM_LAYOUT.md) for compiler and header paths.

Each process has a working directory, initially /. chdir changes it and
getcwd copies its absolute path into a caller-provided buffer. fork and spawn
inherit it; exec preserves it. The shell's cd and pwd run in the shell process.
Relative file paths and executable paths resolve against that directory.
Paths are limited to 255 bytes after prefixing the working directory.

Directory creation is available through `int mkdir(const char *path)` in
`<unistd.h>` and the standalone `/bin/mkdir` command:

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

File deletion is available through `unlink(path)` and `/bin/rm`; empty directories
can be removed through `rmdir(path)` and `/bin/rmdir`. Both commands accept multiple
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

`meinkampf <file>` now opens the full-screen Vim-style editor. See
[Editor](../docs/EDITOR.md) for modes, motions, search, undo, and save commands.
The host checks also cover terminal cursor bounds, erase controls and reverse
video; the QEMU checks exercise visual editing through keyboard input and VGA.

The original line editor starts with `meinkampf --line <file>`.
A missing file opens an empty buffer. Its commands are:

| Command | Action |
| --- | --- |
| `p` | Print the buffer with line numbers |
| `a text` | Append a line (`a` alone appends a blank line) |
| `i N text` | Insert before line N; N may be one past the last line |
| `r N text` | Replace line N |
| `d N` | Delete line N |
| `s` | Save (`w` also works) |
| `q` | Quit, asking before discarding unsaved edits |
| `q!` | Discard edits and quit |
| `h` | Show command help |

Replace `N` with an actual line number, starting at 1: `i 1 hello` inserts
before line 1, and `r 1 hello` replaces line 1. Text after the command separator is literal; quotation
marks are not stripped. The initial version supports files up to 64 KiB and
commands up to 511 bytes. NUL-containing files and oversized files are rejected.
Overlong input is consumed and rejected rather than silently truncated. Loading
and saving preserves a missing final newline; inserted/replaced lines end in a
newline. EOF on a dirty buffer asks for confirmation; if input ends again, the
editor exits with status 1 and leaves the on-disk file unchanged.

Saving writes `<file>.mk-tmp` with exclusive creation, handles partial writes,
closes it, then renames it over the destination. An existing temporary file is
never overwritten. Content-write failures retain the buffer and leave the
original untouched. Failed rename/close can leave a temporary file for recovery.
The replacement inode uses the normal new-file permissions (0644); saving
replaces that path's inode rather than modifying other hard links to it.

The supporting `rename(old, new)` API in `<stdio.h>` currently accepts regular
files within the same directory only. Cross-directory moves return EXDEV;
directories and special files return EPERM. A new destination name must fit in
the source directory entry (otherwise ENOSPC); the editor's temporary name is
longer than its destination, so this restriction does not prevent editor saves.
Existing destinations are replaced, with open handles retaining the old inode
until close. `O_CREAT | O_EXCL` returns EEXIST for existing paths. Rename is
serialized against other filesystem operations, but ext2 metadata updates are
not journaled: power loss or an I/O error during rename can require recovery.

`python3 tests/editor_host.py` tests editor commands, preservation of files
without final newlines, dirty-buffer confirmation, invalid/oversized input,
temporary-file collisions, injected content-write failures, and partial writes.
It compiles the editor with host file-API adapters. The QEMU smoke suite also
creates, edits, saves, reopens and replaces a file with the actual `meinkampf`
command; offline debugfs and e2fsck verify the result. The filesystem helper tests
exclusive creation and rename while a replaced file remains open.

The first TinyCC libc batch adds:

- `realloc` (preserves contents, grows into adjacent free space when possible,
  and leaves the old allocation valid on failure). `realloc(NULL, n)` allocates;
  `realloc(p, 0)` frees and returns NULL.
- `memcpy`, overlap-safe `memmove`, `memset`, and unsigned-byte `memcmp`.
- `strcpy`, `strchr`, `strrchr`, and `strstr`.
- `atoi`, `strtol`, `strtoul`, `strtoll`, and `strtoull`.
- `qsort`, using allocation-free heapsort with bounded stack usage and
  O(n log n) worst-case running time. Equal elements need not remain ordered.

Integer parsing uses C99-style syntax: ASCII whitespace, optional signs, bases
2 through 36, and base 0 detection of octal/decimal/hexadecimal. A hexadecimal
prefix requires a following hex digit. There is no `0b` prefix or locale support.
No conversion leaves endptr at the original input. Overflow saturates to the
appropriate type limit, sets ERANGE, and consumes all valid digits. Invalid
bases return zero with EINVAL and endptr at the original input. Successful
conversions do not clear errno. These functions require valid string pointers.

Run `sh tests/libc_host.sh` for memory/string/conversion/sorting checks and
`sh tests/allocator_host.sh` for allocator/realloc checks (including real backing
storage exhaustion). The QEMU smoke runner builds the test-only `/libccheck`
and verifies the actual i386 long/unsigned-long limits, 64-bit conversions,
sorting, and realloc before the existing memory and filesystem regressions.
The stdio and floating-point additions are described below. The native TinyCC
port now also supplies i386 setjmp/longjmp and installed compiler headers.

The stdio layer now provides opaque `FILE` streams with `stdin`, `stdout`, and
`stderr`; `fopen`/`fdopen`/`fclose`; `fread`/`fwrite`; `fseek`/`ftell`/`rewind`;
`fgetc`/`getc`/`ungetc`/`fgets`; `fputc`/`putc`/`fputs`; `feof`/`ferror`/`clearerr`;
`fileno`, `fflush`, and `remove`. `printf`, `puts`, `putchar`, and `getchar` use
these standard streams.

This first implementation is unbuffered: data is transferred before each call
returns, and `fflush` has no pending output to write. It is not an fsync or a
power-loss durability guarantee. There are 16 stream slots, including the three
standard streams, in addition to the kernel's descriptor limit. Modes `r`, `w`,
`a` and their `+`/`b` combinations are supported. `fdopen` checks descriptor
access modes, does not truncate for `w`, and owns the descriptor only on success.
Append uses the descriptor's O_APPEND flag, shared with dup/fork aliases.

Reads/writes loop over short transfers and return the number of complete
items, not bytes. EOF is sticky until clearerr, successful positioning, rewind,
or ungetc; read errors set the separate error flag. One byte of pushback is
supported. Seeking accounts for that byte, clears EOF on success, and fails on
pipes. Positions remain limited by ZenOS's signed 32-bit lseek API. Callers must
check fwrite/ferror/fclose results; unbuffered output cannot defer an error to
fflush. Use valid streams and valid buffer pointers. There is no setvbuf,
freopen, wide-character I/O, or thread synchronization yet.

`<fcntl.h>` adds F_GETFL and F_SETFL (append only; other status flags are not
supported). Access modes cannot be changed. `open` now accepts an optional
creation-mode argument for source compatibility; creation still uses the
filesystem's fixed 0644 permissions.

Formatting now includes printf/vprintf, fprintf/vfprintf, sprintf/vsprintf,
and snprintf/vsnprintf. Supported conversions: d, i, u, o, x, X, p, c, s, %,
f, F, e, E, g, G, a, and A;
integer lengths hh, h, l, ll, z, t, j; flags -, +, space, #, 0; literal or `*`
width/precision. snprintf terminates when size is nonzero and returns the full
length even when truncated; snprintf(NULL, 0, ...) can measure output. Pointers
use hexadecimal with a 0x prefix (including 0x0 for NULL). Output lengths beyond
INT_MAX fail with EOVERFLOW. Floating formats accept double (including the `l`
modifier) and 80-bit long double (`L`), with rounding, NaN, infinity and signed
zero support. Wide-character, positional and printf %n formats are not
supported and return EOF with EINVAL. Unsupported formats may
leave partial output; they are never silently presented as correctly formatted.
String input scanning provides `sscanf` and `vsscanf`: signed/unsigned integers,
base detection (`%i`), hexadecimal pointers, floating conversions, `%s`, `%c`,
scansets (`%[...]`, `%[^...]`, ascending ranges), whitespace/literals, field
widths, assignment suppression (`*`), and `%n` character counts. Integer lengths
are hh/h/l/ll/j/z/t. Floating destinations are float for `%f`/`%e`/`%g`/`%a`,
double for `%lf`, and long double for `%Lf` (uppercase conversions also work).
The result counts assigned values; suppression and `%n` do not add to it.
Input failure before any assignment returns EOF; a matching failure returns
the assignments so far. Incomplete numeric tokens such as `100e+` fail to
convert. `%c` requires its full width and does not add a terminator.

Use a width at most buffer size minus one for `%s` and scansets. Numeric
scanf destinations must be large enough for the result; use the strto family
when integer range checking is needed. Scanning uses the ASCII C locale;
wide characters, positional arguments, allocation extensions and FILE-based
`scanf`/`fscanf` are not implemented. Invalid/unsupported formats set EINVAL
and return the assignments so far, or EOF when there are none.

```c
int line;
double value;
char name[32];
if (sscanf("12 3.5 source.c", "%d %lf %31s", &line, &value, name) == 3)
    printf("%s:%d = %.2f\n", name, line, value);
```

`strtof`, `strtod`, and `strtold` parse decimal/hexadecimal floats and inf/nan,
return an end pointer, and report range errors through ERANGE. Unlike scanf,
an incomplete exponent is left unconsumed: `strtod("1e+", &end)` returns 1
with end pointing at `e+`. No conversion leaves end at the original string.
`<ctype.h>` supplies ASCII classification/case conversion; `<math.h>` supplies
classification macros and the small x87 helper set used by numeric conversion,
including `ldexp`. It is not a complete mathematical library.

Floating formatting/parsing adapts musl's MIT-licensed conversion algorithms;
see `userlibc/licenses/README.md` for provenance and the retained license.
ZenOS now initializes x87/SSE and saves/restores their state per task. fork
inherits the state; exec/spawn start with the default environment. The kernel
requires an x86 CPU with FXSR and SSE (the default QEMU CPU supports these).
Kernel C code must remain free of floating-point/SSE calculations. Each process
now has a 256 KiB eagerly mapped user stack and an unmapped guard page below it;
ELF images overlapping that reserved region are rejected.

Run `python3 tests/stdio_host.py` for comparisons against host snprintf and
stream tests using real host descriptors, short-transfer adapters and injected
read/write/close errors. The QEMU `/stdiocheck` verifies the i386 varargs ABI,
64-bit formatting, file modes and contents, fdopen ownership/append flags,
EOF/pushback/seek behavior, pipes, broken-pipe errors, and stream exhaustion.
The regular shell smoke runner builds and runs it automatically.

Numeric host checks compare hundreds of deterministic floating-point values,
boundaries, rounding/truncation, long-double extremes, scanf destination types,
field widths, scansets, failures and end pointers against the host libc. The
QEMU `/stdiocheck` also exercises actual float/double/long-double parsing and
formatting, live x87/SSE registers across yields/timer interrupts, fork inheritance
of rounding controls, and exec reset to the default floating-point environment.

Native TinyCC milestone:

```sh
python3 ports/tinycc/build.py
make -C user
sh iso.sh
python3 tests/shell_smoke.py --block-size 4096 --tinycc
```

The optional native compiler checks install the staged `ports/tinycc/root` tree
on the disposable disk. They compile and execute a greeting, multi-file and
object-file programs; verify varargs, 64-bit arithmetic, floating-point parsing
and formatting, and setjmp/longjmp (including longjmp's zero-to-one rule); then
exercise bad syntax, missing headers and output-open failure before another
successful compilation. The regular smoke suite runs afterward. A 4 KiB ext2
image is required because the runtime archives exceed 1 KiB-block file limits.

`python3 ports/tinycc/make_disk.py` creates a separate development
disk at `disk/zenos-dev.img` with `/usr/bin/tcc`, headers, libraries and `/home/user/hello.c`; boot
it together with `myos.iso`. It never overwrites an existing disk. See
`ports/tinycc/README.md` for the boot command, implementation details and limits.

Ctrl+C foreground interruption:

The shell claims the terminal, begins a command group before creating pipeline
children, waits for its children, then finishes the group. Fork and spawn inherit
the group and exec preserves it. Ctrl+C requests termination of all live group
members, including sleeping tasks and blocked keyboard/pipe readers or writers.
The IRQ only marks tasks; cleanup runs at the outer return to user mode, after
kernel work finishes. Interrupted tasks use the normal exit/descriptor cleanup
path and return status 130 through wait. Detached exited descendants are reaped
on subsequent syscalls, including while init waits for a nested shell.

At a shell prompt Ctrl+C discards queued input and the current input line, prints
`^C`, and shows another prompt. Ctrl+D keeps its existing EOF behavior. Nested
shells claim the terminal and restore their parent's ownership on exit. Terminal
reads outside the foreground group return EIO. This is a single-terminal ZenOS
API (`zenos_terminal_claim/begin_job/end_job`), without POSIX signal handlers,
kill, background jobs, or stop/resume support. A command executing kernel code
must finish that operation before termination can be delivered.

The QEMU smoke runner installs a test-only `/intcheck`. It repeatedly interrupts
busy loops, sleepers, blocked pipe writers, and forked children holding allocated
memory and unlinked open files. It also checks two/four-stage pipelines, keyboard
reads, prompt cancellation, nested-shell recovery, and repeated orphan cleanup.
The remaining memory/filesystem regressions and offline e2fsck checks run afterward.
Rebuild the kernel ISO and shell before testing; existing development disks need
the rebuilt `/shell` installed while QEMU is stopped, or use a fresh disk created
with `python3 ports/tinycc/make_disk.py disk/zenos-ctrlc.img`.

Native compiler self-rebuilding:

```sh
python3 ports/tinycc/build.py
make -C user
sh iso.sh
python3 tests/shell_smoke.py --block-size 4096 --selfhost
```

This adds two generations of native TinyCC builds, tests each generated compiler,
and compares their executable bytes after shutdown. It implies `--tinycc` and
uses a disposable disk; add `--layout-only` to omit the broader regressions.
See `ports/tinycc/README.md` for commands to rebuild manually inside ZenOS.

Shell script checks run as part of the smoke suite: comments, blank lines, CRLF,
final lines without a newline, indentation, pipelines/redirection, `&&` success
and short-circuiting, syntax rejection before execution, long-line rejection,
failed commands and `cd`, nested failure propagation, and Ctrl+C recovery.
`--selfhost` now uses the installed TinyCC `build.sh` to rebuild both stages
and run native tests. See [Shell scripts](../docs/SHELL.md).
