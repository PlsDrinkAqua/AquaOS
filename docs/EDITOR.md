# Text Editor

Run `meinkampf file.c` for the full-screen Vim-style editor. It starts in Normal
mode. The status row shows the mode, filename, modified flag, and cursor position.
The viewport follows the cursor vertically and horizontally; tabs display at
four-column stops. The highlighted character is the cursor.

| Keys | Action |
| --- | --- |
| Arrow keys | Move in Normal or Insert mode |
| `h j k l` | Left, down, up, right |
| `w b` | Next / previous word |
| `0 ^ $` | Line start, first nonblank, line end |
| `gg G` | First / last line |
| `i a` | Insert before / after cursor |
| `I A` | Insert at first nonblank / line end |
| `o O` | Open a line below / above |
| Escape | Return to Normal mode |
| `x D` | Delete character / to line end |
| `dd dw d$` | Delete line / word / to line end |
| `yy yw y$` | Yank line / word / to line end |
| `p P` | Paste after / before |
| `u` / `:redo` | Undo / redo |
| `/text`, `n`, `N` | Literal search, next / previous match (wraps) |
| `:number` | Go to line |
| `:w` | Save |
| `:q` | Quit (refuses unsaved changes) |
| `:wq` / `:x` | Save and quit |
| `:q!` | Discard changes and quit |

Press Enter to submit a colon command or search, or Escape to cancel. In Insert
mode, Enter inserts a newline and Backspace deletes backwards, including across
line boundaries. Consecutive typing is one undo step; up to 32 changes are retained.
Saving does not erase undo history.

Arrow keys move without changing modes. Shift+Up/Down still browse terminal
scrollback. Moving in Insert mode starts a new undo group for subsequent edits.

This is a small Vim-style editor, not a full Vim port. Numeric repeat counts,
visual selections, regex search,
syntax highlighting, multiple buffers, and Vim configuration are not implemented.
The display targets the current 80x25 ZenOS VGA terminal. Files remain limited
to 64 KiB; NUL-containing files are rejected. Non-ASCII/control bytes display as
`?` and are preserved unless edited. Ctrl+C terminates the foreground program
through the existing OS behavior and does not save edits.

Saves use an exclusive `.mk-tmp` file and rename it over the target only after
writing and closing successfully. A failed save retains the editing buffer;
an existing recovery file is never overwritten. Disk errors are not journaled.

The original command-based editor is available with `meinkampf --line file.c`.

Build with `make -C user meinkampf` and `sh iso.sh`. Both the new kernel and editor
are needed: the terminal now understands cursor positioning, line/screen erase,
and reverse video. Existing development disks do not automatically refresh their
installed editor. To create a separate disk containing the new editor and the
already staged compiler, run:

```sh
python3 ports/tinycc/make_disk.py disk/zenos-editor.img
qemu-system-i386 -m 128M -cdrom myos.iso \
    -drive file=disk/zenos-editor.img,format=raw,if=ide
```

Verification: `python3 tests/editor_host.py` exercises both editing modes and
failed saves. `python3 tests/shell_smoke.py --block-size 4096` tests the full-screen
workflow through actual keyboard events and VGA output, followed by OS regressions.
