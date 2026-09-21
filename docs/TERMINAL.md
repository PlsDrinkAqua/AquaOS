# Terminal colours

Run `./qemu.sh` to rebuild the compiler, user programs, kernel ISO and development
disk, then boot the new images. Each launch replaces `disk/zenos-dev.img`, so
files created inside the previous guest session are discarded. Stop the previous
QEMU instance before launching again. The fresh disk includes the latest shell;
no manual installation is needed.

Interactive shell input is highlighted as you type: builtins are bright cyan,
external commands are bright magenta, unknown commands are bright red, operators
are yellow, and comments are dark grey. Arguments and redirection targets stay
grey. Command lookup checks ELF magic in `/bin`, `/usr/bin`, then `/`, or uses
an explicitly typed path; this indicates a candidate program, not a guarantee
that loading it will succeed. Highlighting never changes the command buffer.
Tab completion and backspace refresh the colours, including on wrapped lines.

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
cc -I kernel/include -idirafter libk/include tests/tty_scrollback.c -o /tmp/zenos-tty-scrollback
/tmp/zenos-tty-scrollback
cc -Wall -Wextra -Werror -I kernel/include -idirafter libk/include -idirafter userlibc/include tests/shell_highlight.c -o /tmp/zenos-shell-highlight
/tmp/zenos-shell-highlight
```
