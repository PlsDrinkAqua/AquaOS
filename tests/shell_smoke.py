#!/usr/bin/env python3
"""Boot myos.iso against a disposable ext2 disk; requires QEMU and e2fsprogs."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(block_size, tinycc=False):
    if tinycc and block_size != 4096:
        raise ValueError("TinyCC runtime archives require --block-size 4096")
    subprocess.run(["make", "-C", str(ROOT / "user"), "fscheck", "memcheck", "meinkampf", "libccheck", "stdiocheck"], check=True,
                   stdout=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory(prefix="zenos-smoke-") as tmp:
        tmp = Path(tmp)
        disk = tmp / "disk.img"
        with disk.open("wb") as f:
            f.truncate(100 * 1024 * 1024)
        subprocess.run(["mkfs.ext2", "-q", "-F", "-b", str(block_size), str(disk)], check=True)
        hello = tmp / "hello.txt"
        hello.write_text("hello, ZenOS\n")
        bar = tmp / "bar.dat"
        bar.write_text("foo\n")
        commands = ["mkdir /subdir", f"write {hello} /hello.txt", f"write {bar} /subdir/bar.dat"]
        linked = tmp / "linked"
        linked.write_text("linked")
        oversized = tmp / "oversized"
        oversized.write_bytes(b"x" * (5 * 1024 * 1024))
        commands += [f"write {linked} /fscheck-link-a",
                     "ln /fscheck-link-a /fscheck-link-b",
                     "set_inode_field /fscheck-link-a links_count 2",
                     f"write {oversized} /fscheck-oversized",
                     "symlink /fscheck-symlink /hello.txt"]
        programs = ["shell", "hello", "cat", "ls", "echo", "mkdir", "rm", "rmdir", "fscheck", "memcheck", "meinkampf", "libccheck", "stdiocheck"]
        commands += [f"write {ROOT / 'user' / name} /{name}" for name in programs]
        if tinycc:
            tree = ROOT / "ports/tinycc/root"
            if not (tree / "tcc").is_file():
                raise RuntimeError("Run python3 ports/tinycc/build.py first")
            for entry in sorted(tree.rglob("*"), key=lambda p: (len(p.parts), str(p))):
                target = "/" + entry.relative_to(tree).as_posix()
                commands.append(f"mkdir {target}" if entry.is_dir() else f'write "{entry}" "{target}"')
        batch = tmp / "debugfs.txt"
        batch.write_text("\n".join(commands) + "\n")
        subprocess.run(["debugfs", "-w", "-f", str(batch), str(disk)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sockpath = tmp / "qmp"
        proc = subprocess.Popen([
            "qemu-system-i386", "-m", "128M", "-display", "none", "-serial", "none",
            "-no-reboot", "-cdrom", str(ROOT / "myos.iso"),
            "-drive", f"file={disk},format=raw,if=ide",
            "-qmp", f"unix:{sockpath},server=on,wait=off"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 10
            while not sockpath.exists():
                if proc.poll() is not None:
                    raise RuntimeError(proc.stderr.read().decode())
                if time.monotonic() > deadline:
                    raise TimeoutError("QMP socket")
                time.sleep(.05)
            sock = socket.socket(socket.AF_UNIX)
            sock.connect(str(sockpath))
            stream = sock.makefile("rwb", buffering=0)
            json.loads(stream.readline())

            def qmp(name, arguments=None):
                stream.write((json.dumps({"execute": name, "arguments": arguments or {}}) + "\n").encode())
                while True:
                    reply = json.loads(stream.readline())
                    if "error" in reply:
                        raise RuntimeError(reply)
                    if "return" in reply:
                        return reply["return"]

            qmp("qmp_capabilities")

            def screen():
                dump = tmp / "vga.bin"
                qmp("human-monitor-command", {"command-line": f'pmemsave 0xb8000 4000 "{dump}"'})
                data = dump.read_bytes()[::2]
                return "\n".join(data[i:i+80].decode("ascii", errors="replace").rstrip()
                                 for i in range(0, 2000, 80))

            def wait_for(expected, seconds=12):
                deadline = time.monotonic() + seconds
                while time.monotonic() < deadline:
                    text = screen()
                    if "FAIL" in text or "fatal kernel exception" in text:
                        raise AssertionError(text)
                    if expected in text:
                        return text
                    time.sleep(.1)
                raise AssertionError(f"Missing {expected!r}:\n{text}")

            keys = {" ": "spc", "/": "slash", ".": "dot", "-": "minus",
                    "<": "shift-comma", ">": "shift-dot", "|": "shift-backslash"}

            def command(text, expected=None, seconds=12):
                qmp("human-monitor-command", {"command-line": "sendkey ret"})
                time.sleep(.1)
                for char in "clear":
                    qmp("human-monitor-command", {"command-line": f"sendkey {char} 10"})
                    time.sleep(.025)
                qmp("human-monitor-command", {"command-line": "sendkey ret 10"})
                time.sleep(.15)
                for char in text:
                    qmp("human-monitor-command", {"command-line": f"sendkey {keys.get(char, char)} 10"})
                    time.sleep(.025)
                qmp("human-monitor-command", {"command-line": "sendkey ret 10"})
                time.sleep(.35)
                output = wait_for(expected, seconds) if expected else screen()
                if "FAIL" in output or "cannot execute" in output:
                    raise AssertionError(output)
                print(f"PASS: {text}", flush=True)
                return output

            wait_for("GNU GRUB")
            qmp("human-monitor-command", {"command-line": "sendkey ret 10"})
            wait_for("shell at")
            if tinycc:
                command("tcc hello.c -o native-hello", "exited from /tcc with status 0", seconds=60)
                command("./native-hello", "Hello from native TinyCC!")
                command("tcc nativecheck.c companion.c -o nativecheck", "exited from /tcc with status 0", seconds=60)
                command("./nativecheck", "native compiler checks passed")
                command("tcc -c companion.c -o companion.o", "exited from /tcc with status 0", seconds=60)
                command("tcc nativecheck.c companion.o -o nativecheck", "exited from /tcc with status 0", seconds=60)
                command("./nativecheck", "native compiler checks passed")
                command("tcc bad.c -o bad", "exited from /tcc with status 1", seconds=60)
                command("tcc missing.c -o missing", "exited from /tcc with status 1", seconds=60)
                command("tcc hello.c -o /no-dir/hello", "exited from /tcc with status 1", seconds=60)
                command("tcc hello.c -o native-hello", "exited from /tcc with status 0", seconds=60)
                command("./native-hello", "Hello from native TinyCC!")
            # Send editor input without the shell command helper's clear command.
            command("meinkampf /edited", "meinkampf: /edited")

            def editor_input(text, expected):
                for char in text:
                    qmp("human-monitor-command", {"command-line": f"sendkey {keys.get(char, char)} 10"})
                    time.sleep(.025)
                qmp("human-monitor-command", {"command-line": "sendkey ret 10"})
                time.sleep(.2)
                return wait_for(expected)

            editor_input("a first", "meinkampf*>")
            editor_input("a last", "a last")
            editor_input("i 2 middle", "i 2 middle")
            editor_input("r 1 changed", "r 1 changed")
            editor_input("d 3", "d 3")
            editor_input("s", "saved 15 bytes")
            editor_input("q", "exited from /meinkampf with status 0")
            command("cat /edited", "\nchanged\nmiddle\n")
            command("meinkampf /edited", "meinkampf: /edited")
            editor_input("r 2 saved", "meinkampf*>")
            editor_input("s", "saved 14 bytes")
            editor_input("a discard", "a discard")
            editor_input("q", "Discard unsaved changes?")
            editor_input("y", "exited from /meinkampf with status 0")
            command("cat /edited", "\nchanged\nsaved\n")
            command("stdiocheck", "stdio checks passed")
            command("libccheck", "libc core checks passed")
            command("memcheck", "memory reliability checks passed", seconds=90)
            command("fscheck", "filesystem deletion checks passed", seconds=90)
            command("mkdir /remove-me", "exited from /mkdir with status 0")
            command("echo temporary > /remove-me/note")
            command("rmdir /remove-me", "errno=39")
            command("cd /remove-me")
            command("rm note", "exited from /rm with status 0")
            command("cd /")
            command("rmdir /remove-me", "exited from /rmdir with status 0")
            command("rm /remove-me/note", "errno=2")
            # A nested shell must return to its parent (including the parent's cwd).
            command("shell", "shell at")
            command("cd /subdir")
            command("exit", "exited from /shell with status 0")
            command("pwd", "\n/\n")
            command("shell", "shell at")
            qmp("human-monitor-command", {"command-line": "sendkey ctrl-d 10"})
            wait_for("exited from /shell with status 0")
            command("pwd", "\n/\n")

            # Bare cat reads the keyboard until Ctrl+D, then the shell resumes.
            command("cat", "exec /cat")
            for char in "keyboard":
                qmp("human-monitor-command", {"command-line": f"sendkey {char} 10"})
                time.sleep(.025)
            qmp("human-monitor-command", {"command-line": "sendkey ret 10"})
            wait_for("\nkeyboard\n")
            qmp("human-monitor-command", {"command-line": "sendkey ctrl-d 10"})
            wait_for("exited from /cat with status 0")
            command("pwd", "\n/\n")
            command("mkdir /fromshell")
            command("cd /fromshell")
            command("mkdir one two")
            listing = command("ls", "\none/\n")
            if "\ntwo/\n" not in listing:
                raise AssertionError(listing)
            command("cd two")
            command("pwd", "\n/fromshell/two\n")
            command("echo saved > note")
            command("cat note", "\nsaved\n")
            command("cd /")
            command("/cat /hello.txt", "hello, ZenOS")
            command("/echo hello world | /cat > /copy.txt")
            command("cat < /copy.txt", "\nhello world\n")
            command("echo again >> /copy.txt")
            command("cat /copy.txt", "\nhello world\nagain\n")
            command("cat /hello.txt /copy.txt", "\nhello, ZenOS\nhello world\nagain\n")
            command("cd /subdir")
            command("pwd", "\n/subdir\n")
            listing = command("ls", "\nbar.dat\n")
            if "bar.dat/" in listing:
                raise AssertionError(listing)
            command("cat bar.dat", "\nfoo\n")
            command("echo relative > local.txt")
            command("cat < local.txt", "\nrelative\n")
            command("../cat bar.dat", "\nfoo\n")
            command("cd ..")
            command("pwd", "\n/\n")
            command("cd /hello.txt", "cd: cannot change directory")
            command("pwd", "\n/\n")
            qmp("quit")
            proc.wait(timeout=5)
            # Validate actual disk structures, including link counts and bitmaps.
            checked = subprocess.run(["e2fsck", "-fn", str(disk)],
                                     text=True, capture_output=True)
            if checked.returncode:
                raise AssertionError(checked.stdout + checked.stderr)
            persisted = subprocess.run(["debugfs", "-R", "cat /fromshell/two/note", str(disk)],
                                       text=True, capture_output=True, check=True)
            if persisted.stdout != "saved\n":
                raise AssertionError("Directory contents did not persist: " + persisted.stdout)
            edited = subprocess.run(["debugfs", "-R", "cat /edited", str(disk)],
                                    text=True, capture_output=True, check=True)
            if edited.stdout != "changed\nsaved\n":
                raise AssertionError("Editor save did not persist: " + edited.stdout)
            for removed in ["/edited.mk-tmp", "/fscheck-dir", "/remove-me", "/fscheck-link-a", "/fscheck-link-b"]:
                result = subprocess.run(["debugfs", "-R", f"stat {removed}", str(disk)],
                                        text=True, capture_output=True, check=True)
                if "File not found" not in result.stderr:
                    raise AssertionError(f"Deleted path persisted: {removed}: {result.stdout}")
            print("PASS: ext2 integrity and persisted directory contents", flush=True)
            print("All shell smoke checks passed.", flush=True)
        finally:
            proc.terminate()
            proc.wait(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--block-size", type=int, choices=[1024, 4096], default=1024)
    parser.add_argument("--tinycc", action="store_true", help="exercise the staged native TinyCC port")
    args = parser.parse_args()
    run(args.block_size, args.tinycc)
