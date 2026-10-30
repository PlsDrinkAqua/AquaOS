# Software graphics and BMP viewer

Build and boot a fresh development disk with `sh qemu.sh`, then run:

```sh
bmpview /demo.bmp
```

The new disk includes a four-color 160x120 test card at `/demo.bmp`. Press any
key to return to the terminal. Ctrl+C, process exit (including faults), and a
successful exec also release the screen. Old disks need the new binary and a
BMP installed; rebuilding alone does not change existing guest disks.

## Rendering

`<aquaos/surface.h>` supplies a RAM surface with width, height and pixel stride,
plus clipped `aquaos_fill_rect()` and `aquaos_blit()`. Pixels are XRGB8888 integers
`0x00RRGGBB`; the high byte is ignored. Surface storage and stride are the caller's
responsibility. Blits require non-overlapping source/destination storage.

The kernel allocates a full-screen RAM backbuffer after heap initialization.
Fonts render into this buffer; `present()` submits the completed cell rectangle
to device memory, translating RGB fields and respecting device pitch. Full-screen
image submissions are copied to RAM and then presented. This is CPU software
rendering with no vsync/page flipping; backbuffering does not guarantee no tearing.
There is no persistent image layer, resizing, alpha blending or window compositor.

## User display interface

`<aquaos/display.h>` provides:

- `aquaos_display_get_info(&info)`: get framebuffer dimensions.
- `aquaos_display_show(pixels, width, height, x, y)`: show packed XRGB8888 pixels
  on a black background, clipping to the screen. Each call replaces the image.
- `aquaos_display_restore()`: release this process's screen and repaint the terminal.

These are AquaOS syscalls 40–42. Applications never receive a framebuffer address.
The show syscall validates request and pixel pointers, bounds dimensions to 2048
and total pixels to 1,048,576, and snapshots the pixels before acquiring the
screen. Only a foreground terminal job can submit. Screen ownership belongs to
one PID; another PID gets EBUSY, including a forked child. Ownership is not
inherited by fork. A non-owner restore does nothing. Return values follow libc's
`-1`/`errno` convention; unavailable framebuffer reports ENODEV for info/show.

While an image owns the screen, normal text and scrollback continue updating in
the terminal's character model, but terminal pixel rendering is suppressed.
Releasing ownership clears the image and repaints the current terminal view.
This is a temporary full-screen facility, not a window-server protocol.

## BMP subset

`aquaos_bmp_load()` decodes files in userspace into a malloc-owned surface. The
caller frees `image.pixels`. Only Windows BITMAPINFOHEADER (40-byte DIB), one
plane, 24-bit BGR, uncompressed BMP is supported. Both top-down and bottom-up
rows, padding, file offsets, file lengths and optional image-size fields are
checked. Width/height are limited to 2048, total pixels to 1,048,576. Unsupported
formats and malformed/truncated files fail without submitting pixels.

`bmpview` centers images smaller than the screen, clips larger images at the
right and bottom, and waits for one stdin byte before restoring. Use it
interactively; redirected stdin may make it return immediately. PNG/JPEG,
scaling, mouse input, GPU acceleration and a user display server are future work.

## Validation

```sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I kernel/include \
  -idirafter userlibc/include tests/framebuffer_host.c -o /tmp/aquaos-fbcheck
/tmp/aquaos-fbcheck
cc -Wall -Wextra -Werror -fsanitize=address,undefined -idirafter userlibc/include \
  tests/bmp_host.c userlibc/aquaos/bmp.c -o /tmp/aquaos-bmpcheck
/tmp/aquaos-bmpcheck
sh build.sh
python3 tools/iso64.py
python3 tests/graphics_smoke.py
```

The QEMU check uses a disposable disk, verifies screenshot colors for both BMP
orientations, invalid syscall pointers and dimensions, PID ownership, automatic
release on child exit/exec, key/Ctrl+C terminal restoration, and terminal scrollback.
Screenshots are saved in `build/x86_64/graphics-check/`.
