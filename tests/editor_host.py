#!/usr/bin/env python3
"""Test editor behavior with host file APIs; QEMU separately tests ZenOS saves."""
from pathlib import Path
import subprocess
import os
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="zenos-editor-") as directory:
    work = Path(directory)
    editor = work / "editor"
    shim = work / "open.h"
    shim.write_text("""#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
static int editor_open(const char *p, int f) { return open(p, f, 0644); }
static ssize_t editor_write(int fd, const void *p, size_t n) {
    if (getenv("EDITOR_FAIL_WRITE")) { errno = ENOSPC; return -1; }
    if (getenv("EDITOR_SHORT_WRITE") && n > 2) n = 2;
    return write(fd, p, n);
}
#define open editor_open
#define write editor_write
""")
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-include", str(shim),
                    str(ROOT / "user/meinkampf.c"), "-o", str(editor)], check=True)
    path = work / "note"

    def run(commands, expected_status=0, **environment):
        result = subprocess.run([str(editor), str(path)], input=commands, text=True, capture_output=True,
                                env={**os.environ, **environment})
        assert result.returncode == expected_status, result.stdout + result.stderr
        return result.stdout

    run("a first\na last\ni 2 middle\nr 1 changed\nd 3\ns\nq\n")
    assert path.read_bytes() == b"changed\nmiddle\n"
    assert not Path(str(path) + ".mk-tmp").exists()
    output = run("i N hello\ni hello\ns\nq\n")
    assert output.count("use i <line number>") == 2
    assert path.read_bytes() == b"changed\nmiddle\n"
    run("w\nq\n")  # Keep the original save spelling as an alias.
    output = run("d 99\nr 0 bad\na unsaved\nq\nn\np\nq\ny\n")
    assert "Discard unsaved changes?" in output and "3: unsaved" in output
    assert path.read_bytes() == b"changed\nmiddle\n"
    path.write_bytes(b"no newline")
    run("w\nq\n")
    assert path.read_bytes() == b"no newline"
    run("a another\ns\nq\n")
    assert path.read_bytes() == b"no newline\nanother\n"
    run("d 1\nd 1\ns\nq\n")
    assert path.read_bytes() == b""
    output = run("a " + "x" * 600 + "\ns\nq\n")
    assert "command ignored" in output and path.read_bytes() == b""
    temporary = Path(str(path) + ".mk-tmp")
    temporary.write_text("do not overwrite")
    output = run("a new\ns\nq!\n")
    assert "save failed" in output and path.read_bytes() == b""
    assert temporary.read_text() == "do not overwrite"
    temporary.unlink()
    path.write_text("original")
    output = run("r 1 replacement\ns\nq!\n", EDITOR_FAIL_WRITE="1")
    assert "save failed" in output and path.read_text() == "original"
    assert not temporary.exists()
    run("r 1 replacement\ns\nq\n", EDITOR_SHORT_WRITE="1")
    assert path.read_text() == "replacement\n"
    path.write_bytes(b"binary\0data")
    run("", 1)
    assert path.read_bytes() == b"binary\0data"
    path.write_bytes(b"x" * 65537)
    run("", 1)
    assert len(path.read_bytes()) == 65537
    path.write_bytes(b"retained")
    run("a unsaved\n", 1)
    assert path.read_bytes() == b"retained"
    print("Editor behavior checks passed.")
