# Shell

## Command search

Commands without a slash are searched in the colon-separated `PATH` environment
variable, from left to right. The initial default is `/bin:/usr/bin:/` unless
the shell inherits a value. For example:

```sh
export PATH=/bin:/usr/bin:/home/user
```

This lets programs installed in `/home/user` run by name. Relative entries are
resolved against the current directory. Empty entries, including an entirely
empty PATH, search the current directory. `unset PATH` restores the default
search behavior; a newly started shell also exports that default if PATH is
absent. Child shells and external programs inherit exported PATH changes.

Commands containing a slash, such as `./program` or `/bin/kilo`, use that path
directly. No command-location cache is maintained. Missing or non-directory
search entries are skipped, as are candidates exceeding the 255-byte path
limit. A failed executable in an earlier entry stops lookup with status 126;
if no candidate is found, the shell returns 127. Tab completion continues to
complete filenames rather than searching PATH.

## Shell scripts

Run a script with `shell path/to/build.sh`. With no argument, `shell` opens an
interactive prompt. Script commands use the same command lookup, working
directory, pipes and redirection as interactive commands. The script file is
separate from standard input, so programs can still read the terminal or a pipe.
A script's `cd` changes its own working directory, not its parent's.

```sh
# Build and run a program.
cd /home/user
tcc hello.c -o hello && ./hello
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
`exit` ends the current shell with status 0. Script arguments,
loops, `||`, semicolon lists, and background jobs are not
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
sh build.sh
python3 ports/tinycc/build.py
sh iso.sh
python3 tests/shell_smoke.py --block-size 4096 --selfhost
```

## Quoting and escapes

Single and double quotes keep spaces and shell operators inside one argument:
`cat "my notes.txt"` opens one file, and `echo 'hello | world'` prints the pipe
literally. Adjacent quoted and unquoted pieces form one word; `a"b c"d` becomes
`ab cd`. Empty quotes supply an empty argument. Quotes also work with `cd` and
redirection filenames, for example `echo hello > "my notes.txt"`.

Single quotes preserve every enclosed character. Outside quotes, a backslash
makes the next character literal, so `my\ notes.txt` is one word. Inside double
quotes, backslash escapes double quote, backslash, dollar sign, and backtick;
before other characters it is preserved. Command substitution remains unimplemented; backticks are literal text.

Comments and operators are recognized only outside quotes and escapes. A `#`
starts a comment at the beginning of a word; `name#part` stays literal.
Unclosed quotes and trailing unquoted backslashes return syntax status 2 before
any command on the line executes. Multiline quotes and line continuation are
not supported.

## Environment variables

`export NAME=value` sets a variable for this shell and the programs it launches.
Use quotes for spaces, for example `export PROJECT="/home/user/my project"`.
`export NAME` preserves an existing value or creates an empty value. With no
arguments, `export` prints the environment. `unset NAME` removes a variable.
Names use letters, digits, and underscores and cannot begin with a digit.
Bare assignments such as `NAME=value` are not supported yet.

`$NAME` and `${NAME}` expand in unquoted text and double quotes. Single quotes
and escaped dollar signs keep them literal. Unset variables expand to empty
text. Expanded text stays in its argument, including spaces and operators;
field splitting, wildcard expansion, and recursive expansion are not supported.
Expansion works in executable names, arguments, and redirection paths.
Each expanded word and each stage's total argument storage must fit 128 bytes.

`$?` expands to the previous command's exit status, initially 0. It is updated
after each command, including each executed part of an `&&` list. Syntax errors
set it to 2; blank lines and comments preserve it. A pipeline uses its last
stage's status. Scripts retain their existing stop-on-failure behavior.

Child processes inherit the environment; changes in a child do not affect its
parent. Libc provides `getenv`, `setenv`, `unsetenv`, and `environ`. `exec` and
`spawn` pass the current environment; `execve` accepts an explicit environment.
The exec/spawn ABI accepts at most 32 environment entries and 1024 bytes of
strings including terminators, independently of the argument limits. Larger
environments return E2BIG. Rebuild all binaries for the updated startup ABI.
