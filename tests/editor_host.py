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
                    "-idirafter", str(ROOT / "userlibc/include"),
                    str(ROOT / "user/meinkampf.c"), "-o", str(editor)], check=True)
    path = work / "note"

    def run(commands, expected_status=0, **environment):
        result = subprocess.run([str(editor), "--line", str(path)], input=commands, text=True, capture_output=True,
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

    def visual(initial, commands, expected, status=0, **environment):
        path.write_bytes(initial)
        result = subprocess.run([str(editor), str(path)], input=commands, capture_output=True,
                                env={**os.environ, **environment})
        assert result.returncode == status, result.stdout[-2000:]
        assert path.read_bytes() == expected, (path.read_bytes(), expected)
        return result.stdout

    esc = b"\x1b"
    up, down, left, right = (bytes([n]) for n in range(0x80, 0x84))
    visual(b"ab\ncd", right + down + left + up + b"x:wq\n", b"b\ncd")
    visual(b"ab\ncd", b"i" + right + down + left + up + b"X" + esc + b":wq\n", b"Xab\ncd")
    visual(b"ab", b"i" + left + up + right * 5 + down + b"X" + esc + b":wq\n", b"abX")
    visual(b"ab", b"iX" + left + b"Y" + esc + b"u:wq\n", b"Xab")
    visual(b"", b"ihello\nworld" + esc + b":wq\n", b"hello\nworld")
    visual(b"one\ntwo\nthree\n", b"jddpk0iX" + esc + b":wq\n", b"one\nXthree\ntwo\n")
    visual(b"alpha beta\n", b"wdwaB" + esc + b":wq\n", b"alpha B")
    visual(b"first\nlast", b"Goend" + esc + b":wq\n", b"first\nlast\nend")
    visual(b"last", b"yyP:wq\n", b"last\nlast")
    visual(b"last", b"yyp:wq\n", b"last\nlast\n")
    visual(b"base", b"A123" + esc + b"u:wq\n", b"base")
    visual(b"base", b"A123" + esc + b"u:redo\n:wq\n", b"base123")
    visual(b"a\nb\nc", b"ddjdd uu:wq\n", b"a\nb\nc")
    visual(b"abc", b"x:w\nu:q\n:q!\n", b"bc")
    output = visual(b"abc", b"ix" + esc + b":q\n:q!\n", b"abc")
    assert b"Unsaved changes" in output
    visual(b"first second first", b"/second\ndw:wq\n", b"first first")
    visual(b"first second first", b"/first\nNxiZ" + esc + b":wq\n", b"Zirst second first")
    visual(b"1\n2\n3\n", b":3\nInew" + esc + b":wq\n", b"1\n2\nnew3\n")
    visual(b"ab\ncd", b"j0i\b" + esc + b":wq\n", b"abcd")
    visual(b"original", b"iunsaved" + esc, b"original", status=1)
    output = visual(b"original", b"ix" + esc + b":wq\n:q!\n", b"original", EDITOR_FAIL_WRITE="1")
    assert b"Save failed; buffer retained" in output
    visual(b"original", b"A!" + esc + b":wq\n", b"original!", EDITOR_SHORT_WRITE="1")
    temporary.write_text("recovery")
    output = visual(b"original", b":wq\n:q!\n", b"original")
    assert b"Save failed; buffer retained" in output
    assert temporary.read_text() == "recovery"
    temporary.unlink()
    output = visual(b"x" * 65536, b"ia" + esc + b":wq\n", b"x" * 65536)
    assert b"File too large" in output
    visual(b"x" * 200 + b"\nlast", b"$xi!" + esc + b":wq\n", b"x" * 198 + b"!x\nlast")
    visual(b"a\n" * 60, b"Gddggdd:wq\n", b"a\n" * 58)
    terminal = work / "terminal"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "kernel/include"), "-idirafter", str(ROOT / "libk/include"),
                    str(ROOT / "tests/terminal_host.c"), "-o", str(terminal)], check=True)
    subprocess.run([str(terminal)], check=True)
    print("Editor behavior checks passed.")
