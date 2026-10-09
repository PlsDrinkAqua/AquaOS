#!/usr/bin/env python3
"""Boot myos.iso against a disposable ext2 disk; requires QEMU and e2fsprogs."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(block_size, tinycc=False, scrollback_only=False, layout_only=False, selfhost=False, arch="i386", compiler_only=False):
    tinycc = tinycc or selfhost
    if tinycc and block_size != 4096:
        raise ValueError("TinyCC runtime archives require --block-size 4096")
    user_dir = ROOT / ("build/x86_64/user" if arch == "x86_64" else "user")
    iso = ROOT / ("build/x86_64/myos.iso" if arch == "x86_64" else "myos.iso")
    if arch == "i386":
        subprocess.run(["make", "-C", str(user_dir), "fscheck", "memcheck", "meinkampf", "libccheck", "stdiocheck", "intcheck"], check=True,
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
        commands = ["mkdir /bin", "mkdir /subdir", f"write {hello} /hello.txt", f"write {bar} /subdir/bar.dat"]
        history = tmp / "history.txt"
        history.write_text("".join(f"history line {i:04d}\n" for i in range(1100)))
        commands.append(f"write {history} /history.txt")
        linked = tmp / "linked"
        linked.write_text("linked")
        oversized = tmp / "oversized"
        oversized.write_bytes(b"x" * (5 * 1024 * 1024))
        commands += [f"write {linked} /fscheck-link-a",
                     "ln /fscheck-link-a /fscheck-link-b",
                     "set_inode_field /fscheck-link-a links_count 2",
                     f"write {oversized} /fscheck-oversized",
                     "symlink /fscheck-symlink /hello.txt"]
        programs = ["shell", "hello", "cat", "ls", "echo", "mkdir", "rm", "rmdir", "fscheck", "memcheck", "meinkampf", "libccheck", "stdiocheck", "intcheck"]
        if arch == "x86_64": programs.append("abi64check")
        commands += [f"write {user_dir / name} /bin/{name}" for name in programs]
        if tinycc:
            tree = ROOT / ("build/x86_64/root" if arch == "x86_64" else "ports/tinycc/root")
            if not (tree / "usr/bin/tcc").is_file():
                raise RuntimeError("Run python3 ports/tinycc/build.py first")
            for entry in sorted(tree.rglob("*"), key=lambda p: (len(p.parts), str(p))):
                target = "/" + entry.relative_to(tree).as_posix()
                commands.append(f"mkdir {target}" if entry.is_dir() else f'write "{entry}" "{target}"')
        scripts = {
            "script-ok": "# comment\n\n  cd /subdir\n\techo first > /script-result # inline\ncat /script-result | cat > /script-copy\necho second >> /script-result && echo chain works\necho hash#word\npwd",
            "script-fail": "cat /missing-script-input\necho wrong > /script-unwanted\n",
            "script-cd": "cd /missing-script-dir && echo wrong > /script-unwanted\necho wrong > /script-unwanted\n",
            "script-syntax": "echo wrong > /script-unwanted &&\n",
            "script-long": "echo " + "x" * 128 + " > /script-unwanted\n",
            "script-crlf": "# comment\r\necho crlf works\r\n",
            "script-nested": "shell /script-fail\necho wrong > /script-unwanted\n",
            "script-interrupt": "intcheck spin\necho wrong > /script-unwanted\n",
        }
        for name, contents in scripts.items():
            source = tmp / name
            source.write_bytes(contents.encode())
            commands.append(f"write {source} /{name}")
        batch = tmp / "debugfs.txt"
        batch.write_text("\n".join(commands) + "\n")
        subprocess.run(["debugfs", "-w", "-f", str(batch), str(disk)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sockpath = tmp / "qmp"
        proc = subprocess.Popen([
            "qemu-system-" + arch, "-m", "128M", "-display", "none", "-serial", "none",
            "-no-reboot", "-cdrom", str(iso),
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

            keys = {"\t": "tab", " ": "spc", "/": "slash", ".": "dot", "-": "minus",
                    "\x80": "up", "\x81": "down", "\x82": "left", "\x83": "right",
                    "\x84": "home", "\x85": "end", "\x86": "delete", "\b": "backspace",
                    "&": "shift-7", "#": "shift-3", "<": "shift-comma", ">": "shift-dot", "|": "shift-backslash"}

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

            def check_selfhost_artifacts():
                if not selfhost:
                    return
                binaries = []
                for stage in ("stage2", "stage3"):
                    output = tmp / stage
                    subprocess.run(["debugfs", "-R",
                                    f"dump /home/user/tinycc/{stage} {output}", str(disk)],
                                   check=True, capture_output=True)
                    data = output.read_bytes()
                    if not data.startswith(b"\x7fELF"):
                        raise AssertionError(f"{stage} is not an ELF executable")
                    binaries.append(data)
                if binaries[0] != binaries[1]:
                    raise AssertionError("Stage 2 and stage 3 compiler binaries differ")
                print("PASS: stage2 and stage3 are byte-identical; SHA-256 " +
                      hashlib.sha256(binaries[0]).hexdigest(), flush=True)

            wait_for("GNU GRUB")
            qmp("human-monitor-command", {"command-line": "sendkey ret 10"})
            wait_for("shell at")
            if arch == "x86_64": command("abi64check", "PASS: x86-64 ABI")
            if compiler_only:
                if not tinycc: raise ValueError("--compiler-only requires --tinycc")
                command("cd /home/user")
                command("tcc hello.c -o native-hello", "exited from /usr/bin/tcc with status 0", seconds=60)
                command("./native-hello", "Hello from native TinyCC!")
                command("tcc nativecheck.c companion.c -o nativecheck", "exited from /usr/bin/tcc with status 0", seconds=60)
                command("./nativecheck", "native compiler checks passed")
                if selfhost:
                    command("cd tinycc")
                    command("shell build.sh", "native build complete", seconds=180)
                qmp("quit")
                proc.wait(timeout=5)
                subprocess.run(["e2fsck", "-fn", str(disk)], check=True)
                check_selfhost_artifacts()
                return
            command("shell /script-ok", "exited from /bin/shell with status 0")
            command("cat /script-result", "\nfirst\nsecond\n")
            command("cat /script-copy", "\nfirst\n")
            command("pwd", "\n/\n")
            for name, status in (("script-fail", 1), ("script-cd", 1),
                                 ("script-syntax", 2), ("script-long", 2),
                                 ("script-nested", 1), ("missing-script", 1)):
                command("shell /" + name, "exited from /bin/shell with status " + str(status))
            command("shell /script-crlf", "\ncrlf works\n")
            command("echo interactive && echo chain works", "\nchain works\n")
            command("cat /missing-script-input && echo wrong > /script-unwanted")
            command("echo wrong > /script-unwanted &&", "shell: empty command")
            command("cat /script-unwanted", "exited from /bin/cat with status 1")
            command("shell /script-interrupt", "interrupt target ready")
            qmp("human-monitor-command", {"command-line": "sendkey ctrl-c 10"})
            wait_for("exited from /bin/shell with status 130")
            command("echo script interrupt survived", "\nscript interrupt survived\n")
            if selfhost:
                command("cd /home/user")
                command("tcc echo.c -o native-echo", "exited from /usr/bin/tcc with status 0", seconds=60)
                command("./native-echo native utility works", "\nnative utility works\n")
                command("cd tinycc")
                command("shell build.sh", "native build complete", seconds=180)
                wait_for("exited from /bin/shell with status 0")
                for compiler in ("./stage2", "./stage3"):
                    done = "exited from /home/user/tinycc/" + compiler + " with status "
                    command(compiler + " ../hello.c -o greeting", done + "0", seconds=60)
                    command("./greeting", "Hello from native TinyCC!")
                    command(compiler + " -c ../companion.c -o companion.o", done + "0", seconds=60)
                    command(compiler + " ../nativecheck.c companion.o -o check", done + "0", seconds=60)
                    command("./check", "native compiler checks passed")
                    command(compiler + " ../bad.c -o bad", done + "1", seconds=60)
                    command(compiler + " ../missing.c -o missing", done + "1", seconds=60)
                    command(compiler + " ../hello.c -o /no-dir/hello", done + "1", seconds=60)
                    command(compiler + " ../hello.c -o greeting", done + "0", seconds=60)
                    command("./greeting", "Hello from native TinyCC!")
                command("cd /")
            # Exercise completion through actual keyboard input and the shell.
            command("echo ct\x82a", "\ncat\n")
            command("xecho edit\x84\x86\x85s", "\nedits\n")
            command("echo draft\x82\x80\x81x", "\ndrafxt\n")
            command("\x80\x80", "\ndrafxt\n")
            command("mkdir /tabcheck")
            command("cd /tabcheck")
            command("echo completed > abc")
            command("cat a\t", "\ncompleted\n")
            command("echo second > abd")
            command("echo a\t", "\nab\n")
            command("echo absent\t", "\nabsent\n")
            command("mkdir nested")
            command("echo child > nested/item")
            command("cat n\ti\t", "\nchild\n")
            command("cat /tabcheck/n\ti\t", "\nchild\n")
            command("cat < n\ti\t", "\nchild\n")
            command("echo hidden > .secret")
            command("cat .s\t", "\nhidden\n")
            command("cd ..")
            if layout_only:
                command("exit", "shell at")
                listing = command("ls /", "\nbin/\n")
                assert "\ncat\n" not in listing and "\nshell\n" not in listing
                command("echo lookup works", "\nlookup works\n")
                command("/bin/cat /hello.txt", "hello, ZenOS")
                command("cd /subdir")
                command("../bin/cat bar.dat", "\nfoo\n")
                command("shell", "shell at")
                command("exit", "exited from /bin/shell with status 0")
                if tinycc:
                    command("cd /home/user")
                    command("tcc hello.c -o native-hello", "exited from /usr/bin/tcc with status 0", seconds=60)
                    command("./native-hello", "Hello from native TinyCC!")
                    command("tcc nativecheck.c companion.c -o nativecheck", "exited from /usr/bin/tcc with status 0", seconds=60)
                    command("./nativecheck", "native compiler checks passed")
                qmp("quit")
                proc.wait(timeout=5)
                subprocess.run(["e2fsck", "-fn", str(disk)], check=True)
                check_selfhost_artifacts()
                print("PASS: filesystem layout, shell restart and command lookup", flush=True)
                return
            if scrollback_only:
                command("cat /history.txt", "history line 1099")
                live = screen()

                def scroll(key):
                    qmp("human-monitor-command", {"command-line": f"sendkey {key} 10"})
                    time.sleep(.15)
                    return screen()

                older = scroll("pgup")
                assert older != live and "history line" in older
                assert scroll("pgdn") == live
                older = scroll("shift-up")
                assert older.splitlines()[1:] == live.splitlines()[:-1]
                assert scroll("shift-down") == live
                scroll("shift-pgup")
                assert scroll("shift-pgdn") == live
                for _ in range(44):
                    oldest = scroll("pgup")
                assert scroll("pgup") == oldest
                typed = scroll("e")
                assert "history line 1099" in typed and typed != oldest
                scroll("backspace")
                command("clear")
                cleared = screen()
                assert scroll("pgup") == cleared
                print("PASS: scrollback keys, bounds, typing and clear", flush=True)
                qmp("quit")
                proc.wait(timeout=5)
                return
            def interrupt_command(text, exits=1):
                command(text, "exec /bin/cat" if text.startswith("cat") else "interrupt target ready")
                qmp("human-monitor-command", {"command-line": "sendkey ctrl-c 10"})
                wait_for("with status 130")
                time.sleep(.3)
                output = screen()
                if output.count("with status 130") != exits:
                    raise AssertionError(output)
                command("echo recovered", "\nrecovered\n")

            # More than TASK_MAX repetitions expose leaked slots and live resources.
            for i in range(20):
                interrupt_command("intcheck " + ("spin", "sleep", "pipe", "tree")[i % 4],
                                  3 if i % 4 == 3 else 1)
            interrupt_command("cat")
            interrupt_command("cat | cat", 2)
            interrupt_command("intcheck | cat | cat | cat", 4)
            # Ctrl+C cancels a partially typed prompt without executing it.
            command("clear")
            for char in "mkdir unwanted":
                qmp("human-monitor-command", {"command-line": f"sendkey {keys.get(char, char)} 10"})
                time.sleep(.025)
            qmp("human-monitor-command", {"command-line": "sendkey ctrl-c 10"})
            wait_for("^C")
            command("echo prompt survived", "\nprompt survived\n")
            command("shell", "shell at")
            interrupt_command("intcheck sleep")
            for _ in range(8):
                interrupt_command("intcheck tree", 3)
            command("exit", "exited from /bin/shell with status 0")
            interrupt_command("intcheck")
            if tinycc:
                command("cd /home/user")
                command("tcc hello.c -o native-hello", "exited from /usr/bin/tcc with status 0", seconds=60)
                command("./native-hello", "Hello from native TinyCC!")
                command("tcc nativecheck.c companion.c -o nativecheck", "exited from /usr/bin/tcc with status 0", seconds=60)
                command("./nativecheck", "native compiler checks passed")
                command("tcc -c companion.c -o companion.o", "exited from /usr/bin/tcc with status 0", seconds=60)
                command("tcc nativecheck.c companion.o -o nativecheck", "exited from /usr/bin/tcc with status 0", seconds=60)
                command("./nativecheck", "native compiler checks passed")
                command("tcc bad.c -o bad", "exited from /usr/bin/tcc with status 1", seconds=60)
                command("tcc missing.c -o missing", "exited from /usr/bin/tcc with status 1", seconds=60)
                command("tcc hello.c -o /no-dir/hello", "exited from /usr/bin/tcc with status 1", seconds=60)
                command("tcc hello.c -o native-hello", "exited from /usr/bin/tcc with status 0", seconds=60)
                command("./native-hello", "Hello from native TinyCC!")
            # Send editor input without the shell command helper's clear command.
            command("cd /")
            command("meinkampf --line /edited", "meinkampf: /edited")

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
            editor_input("q", "exited from /bin/meinkampf with status 0")
            command("cat /edited", "\nchanged\nmiddle\n")
            command("meinkampf --line /edited", "meinkampf: /edited")
            editor_input("r 2 saved", "meinkampf*>")
            editor_input("s", "saved 14 bytes")
            editor_input("a discard", "a discard")
            editor_input("q", "Discard unsaved changes?")
            editor_input("y", "exited from /bin/meinkampf with status 0")
            command("cat /edited", "\nchanged\nsaved\n")
            command("meinkampf /visual", "NORMAL  /visual")

            def visual_keys(sequence):
                for key in sequence:
                    qmp("human-monitor-command", {"command-line": f"sendkey {key} 10"})
                    time.sleep(.06)

            visual_keys(["i", *"hello", "ret", *"world", "esc"])
            text = wait_for("NORMAL  /visual [+]")
            assert text.splitlines()[0] == "hello", text
            assert text.splitlines()[1] == "world", text
            visual_keys(["u"])
            text = wait_for("NORMAL  /visual  1:1")
            assert "hello" not in text and "world" not in text, text
            visual_keys(["shift-semicolon", *"redo", "ret"])
            wait_for("world")
            visual_keys(["shift-semicolon", "q", "ret"])
            wait_for("Unsaved changes")
            visual_keys(["shift-semicolon", "w", "q", "ret"])
            wait_for("exited from /bin/meinkampf with status 0")
            command("cat /visual", "\nhello\nworld")
            command("meinkampf /visual", "NORMAL  /visual")
            visual_keys(["right", "down", "left", "up", "i", "right", "down", "left", "up", "x", "esc"])
            text = wait_for("NORMAL  /visual [+]")
            assert text.splitlines()[0] == "xhello", text
            visual_keys(["u", "down", "d", "d", "shift-semicolon", "w", "q", "ret"])
            wait_for("exited from /bin/meinkampf with status 0")
            visual_keys(["up", "down", "left", "right"])
            command("cat /visual", "\nhello\n")
            command("stdiocheck", "stdio checks passed")
            command("libccheck", "libc core checks passed")
            command("memcheck", "memory reliability checks passed", seconds=90)
            command("fscheck", "filesystem deletion checks passed", seconds=90)
            command("mkdir /remove-me", "exited from /bin/mkdir with status 0")
            command("echo temporary > /remove-me/note")
            command("rmdir /remove-me", "errno=39")
            command("cd /remove-me")
            command("rm note", "exited from /bin/rm with status 0")
            command("cd /")
            command("rmdir /remove-me", "exited from /bin/rmdir with status 0")
            command("rm /remove-me/note", "errno=2")
            # A nested shell must return to its parent (including the parent's cwd).
            command("shell", "shell at")
            command("cd /subdir")
            command("exit", "exited from /bin/shell with status 0")
            command("pwd", "\n/\n")
            command("shell", "shell at")
            qmp("human-monitor-command", {"command-line": "sendkey ctrl-d 10"})
            wait_for("exited from /bin/shell with status 0")
            command("pwd", "\n/\n")

            # Bare cat reads the keyboard until Ctrl+D, then the shell resumes.
            command("cat", "exec /bin/cat")
            for char in "keyboard":
                qmp("human-monitor-command", {"command-line": f"sendkey {char} 10"})
                time.sleep(.025)
            qmp("human-monitor-command", {"command-line": "sendkey ret 10"})
            wait_for("\nkeyboard\n")
            qmp("human-monitor-command", {"command-line": "sendkey ctrl-d 10"})
            wait_for("exited from /bin/cat with status 0")
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
            command("/bin/cat /hello.txt", "hello, ZenOS")
            command("/bin/echo hello world | /bin/cat > /copy.txt")
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
            command("../bin/cat bar.dat", "\nfoo\n")
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
            for removed in ["/interrupt-live", "/unwanted", "/edited.mk-tmp", "/fscheck-dir", "/remove-me", "/fscheck-link-a", "/fscheck-link-b"]:
                result = subprocess.run(["debugfs", "-R", f"stat {removed}", str(disk)],
                                        text=True, capture_output=True, check=True)
                if "File not found" not in result.stderr:
                    raise AssertionError(f"Deleted path persisted: {removed}: {result.stdout}")
            print("PASS: ext2 integrity and persisted directory contents", flush=True)
            check_selfhost_artifacts()
            print("All shell smoke checks passed.", flush=True)
        finally:
            proc.terminate()
            proc.wait(timeout=5)
            if os.environ.get("ZENOS_SMOKE_DISK"):
                shutil.copy2(disk, os.environ["ZENOS_SMOKE_DISK"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--block-size", type=int, choices=[1024, 4096], default=1024)
    parser.add_argument("--tinycc", action="store_true", help="exercise the staged native TinyCC port")
    parser.add_argument("--scrollback-only", action="store_true", help="exercise terminal scrollback keys")
    parser.add_argument("--layout-only", action="store_true", help="exercise installed paths and shell restart")
    parser.add_argument("--selfhost", action="store_true", help="rebuild TinyCC twice inside ZenOS and test both compilers (implies --tinycc)")
    parser.add_argument("--arch", choices=["i386", "x86_64"], default="i386")
    parser.add_argument("--compiler-only", action="store_true")
    args = parser.parse_args()
    run(args.block_size, args.tinycc, args.scrollback_only, args.layout_only, args.selfhost, args.arch, args.compiler_only)
