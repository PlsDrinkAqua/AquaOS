#!/usr/bin/env python3
"""Boot myos.iso against a disposable ext2 disk; requires QEMU and e2fsprogs."""
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run():
    with tempfile.TemporaryDirectory(prefix="zenos-smoke-") as tmp:
        tmp = Path(tmp)
        disk = tmp / "disk.img"
        with disk.open("wb") as f:
            f.truncate(100 * 1024 * 1024)
        subprocess.run(["mkfs.ext2", "-q", "-F", str(disk)], check=True)
        hello = tmp / "hello.txt"
        hello.write_text("hello, ZenOS\n")
        bar = tmp / "bar.dat"
        bar.write_text("foo\n")
        commands = ["mkdir /subdir", f"write {hello} /hello.txt", f"write {bar} /subdir/bar.dat"]
        programs = ["shell", "hello", "memtest", "yieldtest", "sleeptest", "faulttest",
                    "tasktest", "fdtest", "unixtest", "cat", "ls", "echo", "argtest"]
        commands += [f"write {ROOT / 'user' / name} /{name}" for name in programs]
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
                    if expected in text:
                        return text
                    time.sleep(.1)
                raise AssertionError(f"Missing {expected!r}:\n{text}")

            keys = {" ": "spc", "/": "slash", ".": "dot", "-": "minus",
                    "<": "shift-comma", ">": "shift-dot", "|": "shift-backslash"}

            def command(text, expected=None):
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
                output = wait_for(expected) if expected else screen()
                if "FAIL" in output or "cannot execute" in output:
                    raise AssertionError(output)
                print(f"PASS: {text}", flush=True)
                return output

            wait_for("GNU GRUB")
            qmp("human-monitor-command", {"command-line": "sendkey ret 10"})
            wait_for("shell at")
            command("argtest", "argtest: PASS")
            command("unixtest", "unixtest: PASS")
            command("/cat /hello.txt", "hello, ZenOS")
            command("/echo hello world | /cat > /copy.txt")
            command("cat < /copy.txt", "\nhello world\n")
            command("echo again >> /copy.txt")
            command("cat /copy.txt", "\nhello world\nagain\n")
            command("cat /hello.txt /copy.txt", "\nhello, ZenOS\nhello world\nagain\n")
            command("cd /subdir")
            command("pwd", "\n/subdir\n")
            command("ls", "bar.dat")
            command("cat bar.dat", "\nfoo\n")
            command("echo relative > local.txt")
            command("cat < local.txt", "\nrelative\n")
            command("../cat bar.dat", "\nfoo\n")
            command("cd ..")
            command("pwd", "\n/\n")
            command("cd /hello.txt", "cd: cannot change directory")
            command("pwd", "\n/\n")
            command("fdtest", "fdtest: PASS")
            print("All shell smoke checks passed.", flush=True)
        finally:
            proc.terminate()
            proc.wait(timeout=5)


if __name__ == "__main__":
    run()
