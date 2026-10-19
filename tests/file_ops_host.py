#!/usr/bin/env python3
"""Test actual cp/mv sources with short transfers and injected failures."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='aquaos-file-ops-') as directory:
    work = Path(directory)
    shim = work / 'shim.h'
    shim.write_text(r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
static int test_open(const char *path, int flags, ...) {
    return open(path, flags, 0644);
}
static int test_readdir(int fd, char *name, size_t size) {
    (void)name; (void)size;
    struct stat st;
    if (fstat(fd, &st) < 0) return -1;
    if (S_ISDIR(st.st_mode)) return 0;
    errno = ENOTDIR; return -1;
}
static ssize_t test_read(int fd, void *data, size_t size) {
    if (getenv("FAIL_READ")) { errno = EIO; return -1; }
    if (getenv("SHORT_IO") && size > 7) size = 7;
    return read(fd, data, size);
}
static ssize_t test_write(int fd, const void *data, size_t size) {
    if (getenv("FAIL_WRITE")) { errno = ENOSPC; return -1; }
    if (getenv("SHORT_IO") && size > 3) size = 3;
    return write(fd, data, size);
}
static int test_rename(const char *old, const char *new) {
    if (getenv("FAIL_RENAME")) { errno = EIO; return -1; }
    if (getenv("CROSS_MOVE") && !strstr(old, ".copy-")) { errno = EXDEV; return -1; }
    return rename(old, new);
}
static int test_unlink(const char *path) {
    if (getenv("FAIL_UNLINK") && !strstr(path, ".copy-")) { errno = EIO; return -1; }
    return unlink(path);
}
#define open test_open
#define readdir test_readdir
#define read test_read
#define write test_write
#define rename test_rename
#define unlink test_unlink
''')
    for name in ('cp', 'mv'):
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                        '-include', str(shim), str(ROOT / f'user/{name}.c'),
                        '-o', str(work / name)], check=True)

    def run(name, source, target, status=0, **env):
        result = subprocess.run([str(work / name), source, target], cwd=work,
                                env={**os.environ, **env}, text=True, capture_output=True)
        assert result.returncode == status, result.stdout + result.stderr
        return result.stderr

    source, target = work / 'source', work / 'target'
    data = bytes(range(256)) * 35
    source.write_bytes(data)
    target.write_bytes(b'original')
    for failure in ('FAIL_READ', 'FAIL_WRITE', 'FAIL_RENAME'):
        run('cp', 'source', 'target', 1, **{failure: '1'})
        assert target.read_bytes() == b'original' and source.read_bytes() == data
        assert not list(work.glob('*.copy-*'))
    run('cp', 'source', 'target', SHORT_IO='1')
    assert target.read_bytes() == data
    run('cp', 'source', './source', 1)
    assert source.read_bytes() == data
    os.link(source, work / 'alias')
    run('cp', 'source', 'alias')
    assert source.read_bytes() == data and (work / 'alias').read_bytes() == data
    (work / 'dest').mkdir()
    run('cp', 'source', 'dest')
    assert (work / 'dest/source').read_bytes() == data
    run('cp', 'dest', 'target', 1)
    assert target.read_bytes() == data
    (work / 'target.copy-0').write_bytes(b'recovery')
    source.write_bytes(b'short')
    run('cp', 'source', 'target')
    assert target.read_bytes() == b'short'
    assert (work / 'target.copy-0').read_bytes() == b'recovery'
    (work / 'target.copy-0').unlink()
    run('mv', 'source', 'dest/moved', CROSS_MOVE='1', SHORT_IO='1')
    assert not source.exists() and (work / 'dest/moved').read_bytes() == b'short'
    source.write_bytes(data)
    run('mv', 'source', 'target', 1, CROSS_MOVE='1', FAIL_WRITE='1')
    assert source.read_bytes() == data and target.read_bytes() == b'short'
    message = run('mv', 'source', 'target', 1, CROSS_MOVE='1', FAIL_UNLINK='1')
    assert 'copied destination' in message
    assert source.read_bytes() == data and target.read_bytes() == data
    run('mv', 'source', 'source')
    run('mv', 'source', 'renamed')
    assert not source.exists() and (work / 'renamed').read_bytes() == data
    source.write_bytes(b'')
    run('cp', 'source', 'target')
    assert target.read_bytes() == b''
    for name in ('cp', 'mv'):
        result = subprocess.run([str(work / name), 'source', 'renamed', 'dest'],
                                cwd=work, capture_output=True)
        assert result.returncode == 0, result.stderr
        assert (work / 'dest/source').read_bytes() == b''
        assert (work / 'dest/renamed').read_bytes() == data
    print('PASS: cp/mv contents, short I/O, replacement, paths and failure recovery')
