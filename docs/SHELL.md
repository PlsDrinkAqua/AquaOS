# Shell scripts

Run a script with `shell path/to/build.sh`. With no argument, `shell` opens an
interactive prompt. Script commands use the same command lookup, working
directory, pipes and redirection as interactive commands. The script file is
separate from standard input, so programs can still read the terminal or a pipe.
A script's `cd` changes its own working directory, not its parent's.

```sh
# Build and run a program.
cd /home/user
kirkc hello.c -o hello && ./hello
```

Blank lines are ignored. `#` starts a comment at the beginning of a line or
after a space/tab; `name#part` remains a literal word. LF and CRLF line endings
and a final line without a newline are accepted. Lines are limited to 127
bytes before the newline (including any CR); oversized lines are rejected
without executing their truncated contents. NUL bytes are rejected.

`first && second` runs second only when first succeeds. Multiple `&&` commands
are evaluated left to right. Empty commands, a trailing `&&`, a single `&`, and
malformed pipelines are rejected before any command on that line runs.
These features also work at the interactive prompt.

Scripts stop at the first nonzero command/list status, report the script path
and line number, and return that status to their parent. This includes a failed
command before `&&`: ZenOS scripts always stop on failure, unlike the conditional
exceptions in POSIX `set -e`. A pipeline uses the last stage's exit status;
a pipe/fork/wait setup failure also causes failure. There is no `pipefail` option.
Syntax errors return 2, launch/lookup failures return 127, and Ctrl+C termination
returns 130. Interactive command failures leave the prompt available.

Ctrl+C interrupts the script's current foreground command; its failed status
then stops the script and restores the parent shell's terminal ownership.
`exit` ends the current shell with status 0. Script arguments, variables,
quoting, escapes, loops, `||`, semicolon lists, and background jobs are not
implemented. Builtins such as `cd` and `pwd` must be standalone commands (they
can appear in an `&&` list), not pipeline stages or redirected commands.

## TinyCC build

The TinyCC staging tree includes `/home/user/tinycc/build.sh`:

```sh
cd /home/user/tinycc
shell build.sh
```

It builds stage 2 and stage 3, compiles/runs the greeting and native ABI test,
and prints `native build complete` only after all commands succeed.
Run the QEMU regression suite from the host with:

```sh
make -C user
python3 ports/tinycc/build.py
sh iso.sh
python3 tests/shell_smoke.py --block-size 4096 --selfhost
```
