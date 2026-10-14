#!/usr/bin/env python3
"""Run actual AquaOS stdio against host descriptors and injected I/O failures."""
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
int64_t zt_lseek(int fd, int64_t offset, int whence) {
    off_t n=lseek(fd,offset,whence); if(n<0) zt_errno=errno; return n;
}
int zt_fcntl(int fd, int command, ...) {
    int flags = 0;
    if (command == F_SETFL) { va_list a; va_start(a, command); flags = va_arg(a, int); va_end(a); }
    return result(fcntl(fd, command, flags));
}
int zt_unlink(const char *p) { return result(unlink(p)); }
int zt_rmdir(const char *p) { return result(rmdir(p)); }
''')
    symbols = "stdin stdout stderr fopen fdopen fclose fflush fread fwrite fseek ftell rewind feof ferror clearerr fileno fgetc getc ungetc fgets fputc putc fputs remove printf vprintf fprintf vfprintf sprintf vsprintf snprintf vsnprintf sscanf vsscanf strtof strtod strtold frexpl scalbnl scalbn ldexp fabsl copysignl fmodl".split()
    flags = [f"-D{s}=zt_{s}" for s in symbols + "errno open close read write lseek fcntl unlink rmdir".split()]
    objects = []
    for source in [ROOT / "userlibc/stdio/streams.c", ROOT / "userlibc/stdio/printf.c", ROOT / "userlibc/stdio/sscanf.c",
                   ROOT / "userlibc/stdio/floatscan.c", ROOT / "userlibc/stdlib/strtof.c",
                   ROOT / "userlibc/math/x87.c"]:
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
    assert snprintf(buf, 64, b"%n", C.byref(C.c_int())) == -1
    assert snprintf(buf, 1, b"%2147483647dX", 1) == -1
    # Compare the real formatter against the host libc, including rounded tails,
    # subnormals, non-finite values, large precision, and truncated buffers.
    import math
    import random
    import struct
    rng = random.Random(20260913)
    values = [0.0, -0.0, 0.1, 0.5, 1.5, 2.5, 9.9995, 1e-300, 1e300,
              float.fromhex('0x1.fffffffffffffp1023'), float.fromhex('0x0.0000000000001p-1022'),
              float('inf'), -float('inf'), float('nan')]
    values += [struct.unpack('d', rng.randbytes(8))[0] for _ in range(300)]
    for value in values:
        for fmt in [b'%f', b'%.0f', b'%+.17e', b'%#020.8g', b'%-26.12E', b'%.30g']:
            for capacity in [0, 1, 11, 400]:
                actual = C.create_string_buffer(b'!' * 420)
                expected = C.create_string_buffer(b'!' * 420)
                a = snprintf(actual, capacity, fmt, C.c_double(value))
                b = native.snprintf(expected, capacity, fmt, C.c_double(value))
                assert (a, actual.raw) == (b, expected.raw), (value, fmt, a, b, actual.value, expected.value)
    # Hexadecimal floats can have equivalent, differently normalized spellings.
    for value in values:
        snprintf(buf, 64, b'%a', C.c_double(value))
        decoded = float.fromhex(buf.value.decode())
        assert (math.isnan(value) and math.isnan(decoded)) or decoded == value
    sscanf = fn('sscanf', integer, [string, string])
    native.sscanf.argtypes = [string, string]
    for text, fmt, types in [
        (b' -42 077 0xff tail', b'%d %i %x %4s%n', [C.c_int, C.c_int, C.c_uint, C.c_char * 8, C.c_int]),
        (b'12:345', b'%2d:%2d%n', [C.c_int, C.c_int, C.c_int]),
        (b'abcz-123!', b'%3[a-z]%*[^0-9]%u%n', [C.c_char * 8, C.c_uint, C.c_int]),
        (b']-abc?', b'%5[]a-c-]%c%n', [C.c_char * 8, C.c_char, C.c_int]),
        (b'xy z', b'%2c %c%n', [C.c_char * 2, C.c_char, C.c_int]),
        (b' -128 65535 -1234567890123', b'%hhd %hu %lld%n', [C.c_byte, C.c_ushort, C.c_longlong, C.c_int]),
        (b'17 -3 42', b'%zu %td %jd%n', [C.c_size_t, C.c_ssize_t, C.c_longlong, C.c_int]),
        (b'0x1234', b'%p%n', [C.c_void_p, C.c_int]),
        (b' %5', b' %% %d%n', [C.c_int, C.c_int]),
        (b'1.25 -0x1.8p+2 2.5e-3', b'%f %lf %Lf%n', [C.c_float, C.c_double, C.c_longdouble, C.c_int]),
        (b'', b'%d', [C.c_int]), (b' ', b'%f', [C.c_float]),
        (b'x', b'%d', [C.c_int]), (b'12 x', b'%d %d', [C.c_int, C.c_int]),
        (b'12', b'%*d%d', [C.c_int]), (b'xy', b'%[0-9]', [C.c_char * 8]),
    ]:
        actual = [t() for t in types]; expected = [t() for t in types]
        a = sscanf(text, fmt, *[C.byref(x) for x in actual])
        b = native.sscanf(text, fmt, *[C.byref(x) for x in expected])
        def contents(x):
            # Ignore unspecified padding in x86 extended precision storage.
            return bytes(x)[:10] if isinstance(x, C.c_longdouble) else bytes(x)
        assert a == b and list(map(contents, actual)) == list(map(contents, expected)), (text, fmt, a, b, list(map(contents, actual)), list(map(contents, expected)))
    for text in [b'100e', b'100e+', b'0x1p-', b'infi', b'nan(bad!']:
        value = C.c_double(17)
        assert sscanf(text, b'%lf', C.byref(value)) == 0 and value.value == 17
    value = C.c_double(); consumed = C.c_int(-1)
    assert sscanf(b'1.25e7', b'%4lf%n', C.byref(value), C.byref(consumed)) == 1
    assert value.value == 1.25 and consumed.value == 4
    assert sscanf(b'x', b'%2c', buf) == -1  # A short %c field is input failure.
    for name, ctype in [('strtof', C.c_float), ('strtod', C.c_double)]:
        parser = fn(name, ctype, [string, C.POINTER(ptr)])
        reference = getattr(native, name); reference.restype = ctype
        reference.argtypes = [string, C.POINTER(ptr)]
        inputs = [b'', b'  ', b'+', b'.', b'1e+', b'0x', b'0x.p2', b'0x1p+',
                  b'infinity!', b'infinite', b'nan(tag)', b'nan(', b'-0', b'1e99999',
                  b'-1e-99999', b'4.9406564584124654e-324', b'1.1754943508222875e-38']
        inputs += [repr(v).encode() + b'END' for v in values]
        for text in inputs:
            source = C.create_string_buffer(text); end = ptr(); native_end = ptr()
            a = parser(C.cast(source, string), C.byref(end))
            b = reference(C.cast(source, string), C.byref(native_end))
            assert ((math.isnan(a) and math.isnan(b)) or (a == b and math.copysign(1, a) == math.copysign(1, b))) and end.value == native_end.value, (name, text, a, b, end.value, native_end.value)
        for text in [b'1e99999', b'1e-99999', b'0x1p128' if name == 'strtof' else b'0x1p1024',
                     b'0x1p-150' if name == 'strtof' else b'0x1p-1075']:
            C.c_int.in_dll(lib, 'zt_errno').value = 0
            parser(text, None)
            assert C.c_int.in_dll(lib, 'zt_errno').value == 34
    for text in [b'1.234567890123456789', b'1e-4900', b'1e4900', b'3.36210314311209350626e-4932']:
        actual_ld, expected_ld = C.c_longdouble(), C.c_longdouble()
        assert sscanf(text, b'%Lf', C.byref(actual_ld)) == 1
        assert native.sscanf(text, b'%Lf', C.byref(expected_ld)) == 1
        assert bytes(actual_ld)[:10] == bytes(expected_ld)[:10], text
        for fmt in [b'%.21Lg', b'%+.30Le', b'%.80Lf']:
            actual = C.create_string_buffer(6000); expected = C.create_string_buffer(6000)
            a = snprintf(actual, 6000, fmt, actual_ld)
            b = native.snprintf(expected, 6000, fmt, expected_ld)
            assert (a, actual.value) == (b, expected.value), (text, fmt)
    path = str(work / "data").encode()
    f = fopen(path, b"w+b"); assert f
    assert fseek(f, 5*1024**3, 0) == 0 and ftell(f) == 5*1024**3
    assert fseek(f, 0, 0) == 0
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
