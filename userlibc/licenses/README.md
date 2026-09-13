Numeric conversion provenance
=============================

The following files adapt musl libc sources retrieved on 2026-09-13:

* ../stdio/float_format.inc: fmt_fp from src/stdio/vfprintf.c
  https://git.musl-libc.org/cgit/musl/plain/src/stdio/vfprintf.c
  Changes: ZenOS output sink/helpers and flag mapping, explicit casts/parentheses.
* ../stdio/floatscan.c: src/internal/floatscan.c
  https://git.musl-libc.org/cgit/musl/plain/src/internal/floatscan.c
  Changes: bounded string input in place of musl FILE helpers, internal symbol
  names, explicit casts/parentheses, destination precision/range finalization.

musl-COPYRIGHT retains the upstream MIT license and attribution. The source
URLs track upstream HEAD; the adapted source is retained in this repository.
