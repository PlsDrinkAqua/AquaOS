#!/usr/bin/env python3
"""Run actual ZenOS stdio against host descriptors and injected I/O failures."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="zenos-stdio-") as name:
    work = Path(name)
    shim = work / "shim.c"
    shim.write_text(r'''
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
int zt_errno;
int zt_chunk = 3;
int zt_budget = -1;
int zt_read_error;
int zt_close_error;
static int result(int n) { if (n < 0) zt_errno = errno; return n; }
int zt_open(const char *p, int flags, ...) { return result(open(p, flags, 0644)); }
int zt_close(int fd) {
    int n = result(close(fd));
    if (zt_close_error) { zt_errno = EIO; return -1; }
    return n;
}
int zt_read(int fd, void *p, size_t n) {
    if (zt_read_error) { zt_errno = EIO; return -1; }
    if (zt_chunk && n > (size_t)zt_chunk) n = zt_chunk;
    return result(read(fd, p, n));
}
int zt_write(int fd, const void *p, size_t n) {
    if (!zt_budget) { zt_errno = ENOSPC; return -1; }
    if (zt_chunk && n > (size_t)zt_chunk) n = zt_chunk;
    if (zt_budget > 0 && n > (size_t)zt_budget) n = zt_budget;
    int written = result(write(fd, p, n));
    if (written > 0 && zt_budget > 0) zt_budget -= written;
    return written;
}
int zt_lseek(int fd, int offset, int whence) { return result(lseek(fd, offset, whence)); }
int zt_fcntl(int fd, int command, ...) {
    int flags = 0;
    if (command == F_SETFL) { va_list a; va_start(a, command); flags = va_arg(a, int); va_end(a); }
    return result(fcntl(fd, command, flags));
}
int zt_unlink(const char *p) { return result(unlink(p)); }
int zt_rmdir(const char *p) { return result(rmdir(p)); }
''')
    symbols = "stdin stdout stderr fopen fdopen fclose fflush fread fwrite fseek ftell rewind feof ferror clearerr fileno fgetc getc ungetc fgets fputc putc fputs remove printf vprintf fprintf vfprintf sprintf vsprintf snprintf vsnprintf".split()
    flags = [f"-D{s}=zt_{s}" for s in symbols + "errno open close read write lseek fcntl unlink rmdir".split()]
    objects = []
    for source in [ROOT / "userlibc/stdio/streams.c", ROOT / "userlibc/stdio/printf.c"]:
        obj = work / (source.stem + ".o")
        subprocess.run(["cc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-fPIC",
                        "-I" + str(ROOT / "userlibc/include"), *flags, "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    library = work / "stdio.so"
    subprocess.run(["cc", "-shared", "-fPIC", str(shim), *objects, "-o", str(library)], check=True)
    lib = C.CDLL(str(library))
    native = C.CDLL(None)
    def fn(name, result, args):
        f = getattr(lib, "zt_" + name); f.restype = result; f.argtypes = args; return f
    ptr, size, integer, string = C.c_void_p, C.c_size_t, C.c_int, C.c_char_p
    fopen = fn("fopen", ptr, [string, string]); fclose = fn("fclose", integer, [ptr])
    fread = fn("fread", size, [ptr, size, size, ptr]); fwrite = fn("fwrite", size, [ptr, size, size, ptr])
    fseek = fn("fseek", integer, [ptr, C.c_long, integer]); ftell = fn("ftell", C.c_long, [ptr])
    fgetc = fn("fgetc", integer, [ptr]); ungetc = fn("ungetc", integer, [integer, ptr])
    feof = fn("feof", integer, [ptr]); ferror = fn("ferror", integer, [ptr]); clearerr = fn("clearerr", None, [ptr])
    fdopen = fn("fdopen", ptr, [integer, string]); fileno = fn("fileno", integer, [ptr])
    snprintf = fn("snprintf", integer, [ptr, size, string])
    native.snprintf.argtypes = [ptr, size, string]
    for fmt, value in [(b"%d", -2147483648), (b"%+08d", -42), (b"%-12.5d", 42),
                       (b"% d", 0), (b"%#.0o", 0), (b"%#08x", 42), (b"%#.5o", 9),
                       (b"%.0u", 0), (b"%hhd", 255), (b"%hhu", 511), (b"%hu", 65537),
                       (b"%lld", C.c_longlong(-9223372036854775808)),
                       (b"%llu", C.c_ulonglong(18446744073709551615)),
                       (b"%zu", C.c_size_t(12345)), (b"%.3s", C.c_char_p(b"abcdef"))]:
        for capacity in [0, 1, 2, 5, 64]:
            actual = C.create_string_buffer(b"!" * 80); expected = C.create_string_buffer(b"!" * 80)
            a = snprintf(actual, capacity, fmt, value)
            b = native.snprintf(expected, capacity, fmt, value)
            assert a == b and actual.raw == expected.raw, (fmt, capacity, a, b, actual.raw, expected.raw)
    buf = C.create_string_buffer(64)
    assert snprintf(buf, 64, b"%*.*s:%02d", -8, 3, C.c_char_p(b"abcdef"), 7) == 11
    assert buf.value == b"abc     :07"
    assert snprintf(None, 0, b"%s", C.c_char_p(b"abc")) == 3
    assert snprintf(buf, 64, b"%f", C.c_double(1.0)) == -1
    assert snprintf(buf, 1, b"%2147483647dX", 1) == -1
    path = str(work / "data").encode()
    f = fopen(path, b"w+b"); assert f
    assert fwrite(b"abcdefg", 1, 7, f) == 7 and ftell(f) == 7
    assert fseek(f, 0, 0) == 0 and fgetc(f) == ord('a')
    assert ungetc(ord('Z'), f) == ord('Z') and ftell(f) == 0
    assert fseek(f, 0, 1) == 0 and fgetc(f) == ord('a')
    assert fread(buf, 2, 4, f) == 3 and feof(f) and not ferror(f)
    assert ungetc(ord('!'), f) == ord('!') and not feof(f)
    assert fgetc(f) == ord('!') and fgetc(f) == -1 and feof(f)
    clearerr(f); assert not feof(f)
    C.c_int.in_dll(lib, "zt_read_error").value = 1
    assert fread(buf, 1, 1, f) == 0 and ferror(f) and not feof(f)
    C.c_int.in_dll(lib, "zt_read_error").value = 0
    clearerr(f); assert fclose(f) == 0
    assert not fopen(path, b"rr")
    f = fopen(path, b"r"); assert f
    assert fwrite(b"x", 1, 1, f) == 0 and ferror(f)
    assert not fdopen(fileno(f), b"w")
    assert fclose(f) == 0
    import os
    fd = os.open(path, os.O_RDWR)
    f = fdopen(fd, b"a+"); assert f
    assert fseek(f, 0, 0) == 0 and fwrite(b"END", 1, 3, f) == 3
    assert fclose(f) == 0 and Path(path.decode()).read_bytes() == b"abcdefgEND"
    f = fopen(path, b"w"); assert f
    C.c_int.in_dll(lib, "zt_budget").value = 5
    assert fwrite(b"0123456789", 2, 5, f) == 2 and ferror(f)
    assert Path(path.decode()).read_bytes() == b"01234"
    C.c_int.in_dll(lib, "zt_budget").value = -1
    C.c_int.in_dll(lib, "zt_close_error").value = 1
    assert fclose(f) == -1
    C.c_int.in_dll(lib, "zt_close_error").value = 0
    print("stdio host checks passed")
