#!/usr/bin/env python3
"""Exercise the actual Kilo port with sanitizers and injected file I/O errors."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='zenos-kilo-') as directory:
    work = Path(directory)
    (work / 'zenos').mkdir()
    shutil.copy2(ROOT / 'userlibc/include/zenos/keys.h', work / 'zenos/keys.h')
    harness = work / 'check.c'
    harness.write_text(r'''
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
static int fail_write, short_write;
static int test_open(const char *path, int flags) {
    return open(path, flags, 0644);
}
static ssize_t test_write(int fd, const void *data, size_t size) {
    if (fail_write) { errno = ENOSPC; return -1; }
    if (short_write && size > 2) size = 2;
    return write(fd, data, size);
}
#define open test_open
#define write test_write
#define main kilo_main
''' + f'#include "{ROOT / "user/kilo.c"}"\n' + r'''
#undef main
#undef malloc
#undef realloc
#undef open
#undef write
static void reset(void) {
    for (int i = 0; i < E.numrows; i++) editorFreeRow(&E.row[i]);
    free(E.row); free(E.filename);
    memset(&E, 0, sizeof(E));
    initEditor();
}
static void contents(const char *path, const char *expected) {
    FILE *fp = fopen(path, "r"); assert(fp);
    char buffer[128]; size_t n = fread(buffer, 1, sizeof(buffer)-1, fp);
    buffer[n] = 0; assert(!strcmp(buffer, expected)); assert(!fclose(fp));
}
int main(void) {
    reset(); editorSelectSyntaxHighlight("test.c");
    editorInsertRow(0, "i", 1); /* Short keyword prefix must not overread. */
    editorInsertRow(1, "\t// comment", 11); /* Render offsets differ from bytes. */
    editorInsertRow(2, "", 0);
    editorInsertRow(3, "/*", 2);
    editorInsertRow(4, "", 0);
    editorInsertRow(5, "int x;", 6);
    editorInsertRow(6, "*/", 2);
    assert(E.row[5].hl[0] == HL_MLCOMMENT);
    editorDelRow(3);
    for (int i = 0; i < E.numrows; i++) assert(E.row[i].idx == i);
    assert(E.row[4].hl[0] == HL_KEYWORD2);
    editorInsertRow(E.numrows, "\"\\", 2); /* Incomplete escaped string. */
    editorRowDelChar(&E.row[0], 0);
    assert(E.row[0].size == 0 && E.row[0].rsize == 0);
    reset();
    FILE *fp = fopen("document", "w"); assert(fp);
    assert(fputs("first\r\nsecond", fp) >= 0); assert(!fclose(fp));
    assert(editorOpen("document") == 0);
    assert(E.numrows == 2 && !strcmp(E.row[1].chars, "second"));
    editorDelRow(1); E.dirty = 1;
    short_write = 1; assert(editorSave() == 0); short_write = 0;
    contents("document", "first\n");
    editorInsertRow(1, "new", 3);
    fail_write = 1; assert(editorSave() == 1); fail_write = 0;
    assert(E.dirty); contents("document", "first\n");
    assert(access("document.kilo-tmp", F_OK) != 0);
    fp = fopen("document.kilo-tmp", "w"); assert(fp);
    assert(fputs("recovery", fp) >= 0); assert(!fclose(fp));
    assert(editorSave() == 1 && E.dirty);
    contents("document.kilo-tmp", "recovery");
    contents("document", "first\n");
    assert(!unlink("document.kilo-tmp"));
    editorDelRow(1); editorDelRow(0);
    assert(editorSave() == 0); contents("document", "");
    reset(); puts("PASS: Kilo syntax, row indexing, loading, short writes and failed saves");
    return 0;
}
''')
    binary = work / 'check'
    subprocess.run([os.environ.get('CC', 'cc'), '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-fno-pie', '-no-pie', '-I' + str(work), str(harness),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], cwd=work, check=True)
