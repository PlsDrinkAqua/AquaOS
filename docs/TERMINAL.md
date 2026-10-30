# Terminal colours

Run `sh qemu.sh` to rebuild and boot a fresh x86-64 development disk.
New guest images use `disk/aquaos64.XXXXXX/`; existing images keep their original paths.

Interactive shell input is highlighted as you type: builtins are bright cyan,
external commands are bright magenta, unknown commands are bright red, operators
are yellow, and comments are dark grey. Arguments and redirection targets stay
grey. Command lookup checks ELF magic in `/bin`, `/usr/bin`, then `/`, or uses
an explicitly typed path; this indicates a candidate program, not a guarantee
that loading it will succeed. Highlighting never changes the command buffer.
Tab completion and backspace refresh the colours, including on wrapped lines.

Left/Right move through input; Home/End jump to its boundaries. Typing inserts
at the cursor, Backspace removes the preceding character, and Delete removes
the next character. Tab completes the prefix before the cursor and preserves
the remaining text. Up/Down browse the last 32 submitted lines (up to 127 bytes
each), with consecutive duplicates omitted. Down past the newest entry restores
the unfinished draft and its cursor. History lasts for the process lifetime.
Ctrl+C cancels without adding history; Enter submits the entire line.

The editor uses AquaOS-specific `CSI 1;N~` / `CSI 2;N~` output sequences to move
backward/forward by N cells across wrapped rows without erasing text. These are
terminal extensions, not standard ANSI cursor movement. A reverse-video block follows
the editing position and is hidden while viewing scrollback.
The block swaps the cell's foreground/background on screen only; moving it
restores the original character and colours. The hardware underline is disabled.
Navigation moves only the cursor. Text edits use AquaOS `CSI 3~` / `CSI 4~`
begin/end redraw sequences: intermediate changes stay in the terminal backing
buffer until the completed line and final cursor position are ready to display.

Programs can print ANSI SGR sequences through `printf`, `fputs`, or `write`:

```c
printf("\033[31mRed text\033[0m\n");
printf("\033[1;32;44mBright green on blue\033[0m\n");
```

Supported attributes:

| Codes | Effect |
| --- | --- |
| 0 (or empty) | Reset to light grey on black |
| 1 / 22 | Enable / disable bold (bright foreground) |
| 7 / 27 | Enable / disable reverse video |
| 30-37 | Standard foreground |
| 90-97 | Bright foreground |
| 40-47 | Standard background |
| 39 / 49 | Default foreground / background |

Colour order is black, red, green, yellow, blue, magenta, cyan, white.
VGA renders standard yellow as brown and standard white as light grey.
Backgrounds use eight colours; bright backgrounds, indexed colours and RGB
colours are unsupported. Reverse video reduces a bright foreground to its
base colour when using it as a background to avoid enabling VGA blink.
Attributes persist until changed or reset, including across writes. Existing
text retains its colours when scrolling. End coloured output with `\033[0m`.
The interactive shell resets colours before each prompt and before user input.

Escape sequences are interpreted by the terminal, so redirected output keeps
the original bytes. The shell does not interpret backslash escapes in commands.

Run the terminal regression checks from the repository root:

```sh
cc -I kernel/include -idirafter libk/include tests/tty_scrollback.c -o /tmp/aquaos-tty-scrollback
/tmp/aquaos-tty-scrollback
cc -Wall -Wextra -Werror -I kernel/include -idirafter libk/include -idirafter userlibc/include tests/shell_highlight.c -o /tmp/aquaos-shell-highlight
/tmp/aquaos-shell-highlight
```

## Framebuffer console

The normal ISO requests a 640x480, 32-bit RGB framebuffer through Multiboot v1
and preserves GRUB's graphics mode. The kernel validates the reported format,
pitch and size, then maps the framebuffer supervisor-only and uncached in a
separate device window. RGB channel ordering follows the boot information.
The 80x25 terminal is rendered with an embedded public-domain 8x16 bitmap font,
centered on the screen. Existing colours, editing, cursor and 1000-line
scrollback retain their character-based representation. Non-ASCII bytes render
as question marks. There is no GPU acceleration, mouse or window server yet.
`bmpview` provides temporary full-screen image display; see [graphics](GRAPHICS.md).

A character mirror (`terminal_screen`) in kernel RAM lets QEMU regression tools
inspect terminal contents without reading VGA text memory in graphics mode.
`tests/console_address.py` resolves its physical address from the built ELF.
The legacy VGA backend is retained when no supported framebuffer is supplied;
this cannot display text on firmware that has already selected an unsupported
graphics mode.

Host renderer validation:

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I kernel/include \
  -idirafter userlibc/include tests/framebuffer_host.c -o /tmp/aquaos-fbcheck
/tmp/aquaos-fbcheck
```

## Full-screen redraws

The terminal recognizes DEC `CSI ?25l` / `CSI ?25h` cursor hide/show sequences,
including sequences split across writes. Unknown private modes are ignored.

A userspace stdout/stderr write is rendered as one transaction across all of
its 256-byte syscall copy chunks. Task preemption is disabled until the terminal
model is updated and committed. Explicit shell begin/end redraw sequences still
work and can defer a transaction's presentation further. Nested kernel updates
flush only on the outermost end.

At commit, the terminal compares visible character cells, renders changed glyphs
and the final cursor into the RAM backbuffer, then submits their bounding rectangle.
Unchanged cells avoid glyph rendering; forced refresh still repaints after image
viewing or framebuffer initialization. This removes intermediate character/cursor
updates during Kilo scrolling without changing the editor. It does not add hardware
vsync or page flipping, and does not combine separate application write calls into
one frame.

Run `python3 tests/kilo_scroll.py` after building the ISO and staging TinyCC. It
scrolls the actual `tcc.h` through 1500 downward steps, a rapid input burst, and
upward movement, comparing source rows and full terminal framebuffer pixels to
the embedded font and VGA palette.
