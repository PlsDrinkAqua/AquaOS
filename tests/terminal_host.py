#!/usr/bin/env python3
"""Run the kernel terminal checks independently of any editor."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="zenos-terminal-") as directory:
    binary = Path(directory) / "terminal"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "kernel/include"),
                    "-idirafter", str(ROOT / "libk/include"),
                    str(ROOT / "tests/terminal_host.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print("Terminal behavior checks passed.")
