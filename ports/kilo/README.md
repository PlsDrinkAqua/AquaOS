# Kilo on ZenOS

Kilo is built and installed as `/bin/kilo` alongside the other user commands.
The adapted source is `user/kilo.c`; normal builds require no download.

```sh
sh qemu.sh
```

Inside ZenOS:

```sh
kilo /home/user/hello.c
```

Type directly to edit. Ctrl+S saves, Ctrl+F searches, and Ctrl+Q quits.
Search accepts arrows to move between matches, Enter to accept, and Escape to
cancel. Arrow keys, Home, End, Backspace, Delete and Tab work while editing.
With unsaved changes, quitting requires four consecutive Ctrl+Q presses; any
other action resets the confirmation count. Ctrl+C uses ZenOS foreground
termination and exits without saving. Ctrl+D ends terminal input.

Existing disk images need the new `/bin/kilo` and rebuilt kernel. A fresh disk
from `sh qemu.sh` includes both; previous development disks are retained.
The binary's license is installed at `/etc/kilo.COPYING` on fresh disks.

## Implementation

Based on [antirez/kilo](https://github.com/antirez/kilo) at commit
`323d93b29bd89a2cb446de90c4ed4fea1764176e`. Upstream's BSD license is retained
in the source and [COPYING](COPYING).

The ZenOS port replaces termios, terminal queries and escape-sequence keyboard
decoding with native single-byte key events and the current fixed VGA terminal.
It uses 23 editing rows and two status rows, reserving the last column to avoid
immediate VGA line wrapping. ANSI colours provide C/C++ syntax highlighting.
Rendering uses LF alone because ZenOS newline resets the column; CR+LF would
print a carriage-return glyph and wrap the status bar, scrolling away file text.
Status messages remain until another message replaces them, without a clock API.

A local dynamic line reader replaces getline. Loading rejects NUL bytes and
files exceeding 1 MiB. Saves normalize line endings to LF and add a final newline
to every row, matching Kilo's row-based document model. An empty document saves
as an empty file. Kilo does not provide Vim modes or undo.

Saving creates `<filename>.kilo-tmp` exclusively, handles short writes, checks
close, and renames the completed file over the destination. Failed saves retain
the buffer and its modified flag. Existing temporary files are never overwritten;
close or rename failures retain the temporary file for recovery. This uses the
non-journaled filesystem operations provided by ZenOS.

Additional fixes cover allocation failure, short keyword comparisons, tabbed
comment highlighting, incomplete escaped strings, row indices after deletion,
and multiline comment propagation without recursive stack growth.

## Verification

```sh
python3 tests/kilo_host.py
sh build.sh
sh iso.sh
python3 tests/shell_smoke.py --block-size 4096 --kilo-only
```

The host test uses address/undefined-behavior sanitizers and injects short writes
and write failures. The QEMU test exercises actual keyboard controls, editing,
search, reopen, shortening a file, unsaved-change protection, recovery-file
collision, disk persistence and offline ext2 consistency. It also checks that
opened file text, including a long first row, stays visible at the top of the
screen. Kilo checks also run
in the regular shell smoke suite.
