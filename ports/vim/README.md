# Vim port feasibility audit

Run from the repository root after building AquaOS:

```sh
sh build.sh
python3 ports/vim/probe.py
```

The probe downloads upstream Vim v9.1.0000 with SHA-256 verification, configures
a tiny terminal build, checks headers, and links independent symbol probes
against the existing AquaOS libc and startup object. It excludes host include
directories and rejects explicit host libraries. All downloaded source and logs
are under `build/x86_64/vim-probe`. Nothing is installed on guest disks.

## Findings

The configure attempt stops because `tgetent` and a terminal library are missing.
A separate compilation of Vim's Unix headers stops at missing `sys/types.h`.
Independent probes continue past these blockers:

| Area | Missing headers or symbols | Porting direction |
| --- | --- | --- |
| File metadata | `sys/types.h`, `sys/stat.h`; `stat`, `fstat`, `lstat`, `access`, `chmod`, `fchmod`, `utime` | Expose real filesystem metadata; decide permission and timestamp support. |
| Directory iteration | `dirent.h`; `opendir`, `closedir` | Wrap AquaOS directory descriptors in a POSIX-compatible interface. |
| Terminal capabilities | `termcap.h`; `tgetent`, `tgetstr`, `tgetnum`, `tgetflag`, `tgoto`, `tputs` | Supply a small termcap implementation or patch Vim to use its built-in terminal descriptions. |
| Terminal control | `termios.h`, `sys/ioctl.h`; `tcgetattr`, `tcsetattr`, `ioctl`, `isatty` | Adapt keyboard input, terminal dimensions and mode control to AquaOS. |
| Input waiting | `sys/select.h`, `poll.h`; `select`, `poll` | Add readiness/timeouts or implement a AquaOS-specific Vim input wait. |
| Time | `time.h`, `sys/time.h`; `time`, `gettimeofday` | Provide clock APIs; distinguish wall clock from timeout timing. |
| Processes and signals | `signal.h`, `sys/wait.h`; `signal`, `sigaction`, `kill`, `waitpid`, `getpid` | Adapt interrupt handling and subprocess execution; initially disable optional job/suspend behavior. |
| Other compatibility | `pwd.h`, `utime.h`, `wchar.h`, `wctype.h`; `fsync` | Audit guarded fallbacks and required multibyte code before implementing additional APIs. |

Successful symbol probes: `read`, `write`, `open`, `close`, `rename`, `fork`,
`getenv`, `malloc`, `fcntl`. This confirms link availability only, not full
POSIX behavior. In particular, AquaOS currently exposes only `F_GETFL` and
`F_SETFL` for `fcntl`.

Existing interfaces also differ from Unix: `mkdir(path)` has no mode argument;
`readdir(fd, name, size)` is not POSIX `readdir(DIR *)`; `wait(pid)` returns a
status instead of using POSIX's status-pointer interface. `ssize_t` and `off_t`
are currently 32-bit `int` even on this LP64 target. These require deliberate
ABI decisions or port-local adapters.

An exploratory configuration also confirmed that this Vim revision rejects
`--disable-multibyte`; tiny does not remove every compatibility requirement.

## Recommended sequence

1. Add `sys/types.h` and real `stat`/`fstat` support with `sys/stat.h`.
2. Add directory wrappers and basic terminal identification/dimensions.
3. Choose a termcap strategy and a terminal/input adapter, then rerun configure.
4. Compile actual Vim objects and resolve subsequent type/API failures.
5. Link a static executable with AquaOS's linker script; boot it in QEMU and
   verify open, insert, save, quit, timeout behavior and Ctrl+C handling.

This is a first-blocker audit, not an exhaustive dependency list. Configure did
not finish, Vim objects did not build, and no Vim executable was linked or run.
Optional and platform-guarded source references do not all need implementations.
The symbol probes use address references rather than calls, so they do not
validate signatures or behavior. Probe results describe the existing built
libc; rebuild before auditing changes to libc.

Machine-readable results: `build/x86_64/vim-probe/results.json`.
Configure log: `build/x86_64/vim-probe/configure.log`.
Compiler log: `build/x86_64/vim-probe/vim-header.log`.
Each independent header/symbol probe also retains its diagnostic log.
