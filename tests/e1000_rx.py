#!/usr/bin/env python3
"""Boot the built ISO and inject Ethernet frames through a QEMU socket backend."""
import json
import os
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAC = bytes.fromhex('060706070607')
SOURCE = bytes.fromhex('020000000001')


def main():
    with tempfile.TemporaryDirectory(prefix='aquaos-rx-') as folder:
        tmp = Path(folder)
        disk = tmp / 'disk.img'
        with disk.open('wb') as stream:
            stream.truncate(32 * 1024 * 1024)
        subprocess.run(['mkfs.ext2', '-q', '-F', '-b', '4096', str(disk)], check=True)
        batch = tmp / 'install.txt'
        batch.write_text(f'mkdir /bin\nwrite {ROOT}/build/x86_64/user/shell /bin/shell\n')
        subprocess.run(['debugfs', '-w', '-f', str(batch), str(disk)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        host, guest = socket.socketpair()
        host.settimeout(5)
        path = tmp / 'qmp'
        proc = subprocess.Popen([
            'qemu-system-x86_64', '-m', '128M', '-display', 'none', '-serial', 'none',
            '-no-reboot', '-cdrom', str(ROOT / 'build/x86_64/aquaos.iso'),
            '-drive', f'file={disk},format=raw,if=ide,snapshot=on',
            '-netdev', f'socket,id=n0,fd={guest.fileno()}',
            '-device', 'e1000,netdev=n0,mac=52:54:00:12:34:56',
            '-qmp', f'unix:{path},server=on,wait=off'],
            pass_fds=(guest.fileno(),), stderr=subprocess.PIPE)
        guest.close()
        try:
            deadline = time.monotonic() + 10
            while not path.exists():
                if proc.poll() is not None:
                    raise RuntimeError(proc.stderr.read().decode())
                if time.monotonic() > deadline:
                    raise TimeoutError('QMP socket')
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as control:
                control.settimeout(10)
                control.connect(str(path))
                with control.makefile('rwb', buffering=0) as stream:
                    stream.readline()

                    def qmp(command, arguments=None):
                        stream.write((json.dumps({'execute': command,
                                                 'arguments': arguments or {}}) + '\n').encode())
                        while True:
                            reply = json.loads(stream.readline())
                            if 'error' in reply:
                                raise RuntimeError(reply)
                            if 'return' in reply:
                                return reply['return']

                    qmp('qmp_capabilities')
                    time.sleep(1)
                    qmp('human-monitor-command', {'command-line': 'sendkey ret'})

                    def screen():
                        dump = tmp / 'vga.bin'
                        qmp('human-monitor-command', {
                            'command-line': f'pmemsave 0xb8000 4000 "{dump}"'})
                        data = dump.read_bytes()[::2]
                        return '\n'.join(data[i:i+80].decode('ascii', errors='replace')
                                         for i in range(0, 2000, 80))

                    def wait_for(expected):
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline:
                            text = screen()
                            if 'fatal kernel exception' in text:
                                raise AssertionError(text)
                            if expected in text:
                                return text
                            time.sleep(.05)
                        raise AssertionError(f'Missing {expected}:\n{text}')

                    wait_for('About to enter user mode')
                    assert 'E1000: RX ready' in screen()

                    def send(destination, length=60):
                        frame = destination + SOURCE + bytes.fromhex('88b5')
                        frame += bytes(length - len(frame))
                        host.sendall(struct.pack('!I', len(frame)) + frame)

                    # Filter traffic addressed to an unrelated unicast MAC.
                    send(bytes.fromhex('020000000099'))
                    time.sleep(.2)
                    assert 'E1000: RX #' not in screen()
                    # More than one ring's worth verifies descriptor recycling.
                    for number in range(1, 41):
                        send(MAC, 60 + number)
                        text = wait_for(f'E1000: RX #{number} len={60 + number} type=0x88b5')
                        assert 'src=02:00:00:00:00:01 dst=06:07:06:07:06:07' in text
                    send(bytes([255]) * 6)
                    text = wait_for('E1000: RX #41 len=60 type=0x88b5')
                    assert 'dst=ff:ff:ff:ff:ff:ff' in text
                    send(MAC, 1514)
                    wait_for('E1000: RX #42 len=1514 type=0x88b5')
                    print('PASS: unicast, broadcast, filtering, full-size frame, and ring wrap')
        finally:
            host.close()
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


if __name__ == '__main__':
    try:
        env = dict(os.environ)
        env['AQUAOS64_CC'] = env.get('AQUAOS64_CC', 'gcc') + ' -DAQUAOS_NET_DEBUG'
        subprocess.run(['python3', 'tools/build64.py'], cwd=ROOT, env=env, check=True)
        subprocess.run(['python3', 'tools/iso64.py'], cwd=ROOT, check=True)
        main()
    finally:
        subprocess.run(['python3', 'tools/build64.py'], cwd=ROOT, check=True)
        subprocess.run(['python3', 'tools/iso64.py'], cwd=ROOT, check=True)
