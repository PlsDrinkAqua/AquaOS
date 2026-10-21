#!/usr/bin/env python3
"""Real host HTTP server through QEMU user networking and userspace TCP."""
import json
import socket
import struct
import subprocess
import tempfile
import time
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAC = bytes.fromhex('060706070607')
SOURCE = bytes.fromhex('020000000001')
GATEWAY = bytes.fromhex('020000000006')


def response(path):
    body = bytes((i*37+11)&255 for i in range(20000)) if path == '/large' else b'AquaOS HTTP transfer PASS\n'
    return (b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(body)).encode()+
            b'\r\nConnection: close\r\n\r\n'+body)


class HttpServer:
    def __init__(self):
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1',0))
        self.socket.listen()
        self.socket.settimeout(.1)
        self.port = self.socket.getsockname()[1]
        self.refused = socket.socket()
        self.refused.bind(('127.0.0.1',0))
        self.refused_port = self.refused.getsockname()[1]
        self.paths = []
        self.error = None
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.serve, daemon=True)

    def serve(self):
        try:
            while not self.stop.is_set():
                try:
                    connection, _ = self.socket.accept()
                except socket.timeout:
                    continue
                with connection:
                    connection.settimeout(10)
                    request = b''
                    while b'\r\n\r\n' not in request:
                        data = connection.recv(4096)
                        assert data, request
                        request += data
                        assert len(request) < 4096
                    method, path, version = request.split(b'\r\n',1)[0].split()
                    assert method == b'GET' and version == b'HTTP/1.0'
                    assert f'Host: 6.6.6.6:{self.port}\r\n'.encode() in request
                    path = path.decode()
                    self.paths.append(path)
                    if path == '/reset':
                        connection.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0))
                    else:
                        connection.sendall(response(path))
        except Exception as error:
            self.error = error

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *unused):
        self.stop.set()
        self.thread.join(timeout=12)
        self.socket.close()
        self.refused.close()
        assert not self.thread.is_alive()
        if self.error:
            raise RuntimeError('Host HTTP fixture failed') from self.error


def main(server, no_nic=False):
    with tempfile.TemporaryDirectory(prefix='aquaos-http-') as folder:
        tmp = Path(folder)
        disk = tmp / 'disk.img'
        with disk.open('wb') as stream:
            stream.truncate(32 * 1024 * 1024)
        subprocess.run(['mkfs.ext2', '-q', '-F', '-b', '4096', str(disk)], check=True)
        batch = tmp / 'install.txt'
        batch.write_text(f'mkdir /bin\nwrite {ROOT}/build/x86_64/user/shell /bin/shell\nwrite {ROOT}/build/x86_64/user/httpget /bin/httpget\nwrite {ROOT}/build/x86_64/user/tcpcheck /bin/tcpcheck\n')
        subprocess.run(['debugfs', '-w', '-f', str(batch), str(disk)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        path = tmp / 'qmp'
        proc = subprocess.Popen([
            'qemu-system-x86_64', '-m', '128M', '-display', 'none', '-serial', 'none',
            '-no-reboot', '-cdrom', str(ROOT / 'build/x86_64/aquaos.iso'),
            '-drive', f'file={disk},format=raw,if=ide',
            *(['-nic', 'none'] if no_nic else ['-netdev', 'user,id=n0,net=6.6.6.0/24,host=6.6.6.6,dhcpstart=6.6.6.15',
            '-device', 'e1000,netdev=n0,mac=52:54:00:12:34:56']),
            '-qmp', f'unix:{path},server=on,wait=off'],
            stderr=subprocess.PIPE)
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

                    wait_for('shell at')
                    # No Enter after shell startup: delayed boot network work
                    # must leave the first prompt visible and ready to type.
                    time.sleep(4)
                    assert screen().rstrip().endswith('AquaOS>'), screen()
                    assert 'task: pid=' not in screen(), screen()

                    def type_line(line):
                        for c in line:
                            key = {' ': 'spc', '.': 'dot', '-': 'minus', '/': 'slash', '>': 'shift-dot'}.get(c, c)
                            qmp('human-monitor-command', {'command-line': 'sendkey ' + key})
                            time.sleep(.06)
                        qmp('human-monitor-command', {'command-line': 'sendkey ret'})

                    def command(line, expected):
                        # Remove previous results so identical failures cannot
                        # satisfy the next command's assertion prematurely.
                        type_line('clear')
                        time.sleep(.2)
                        if expected != 'AquaOS>':
                            assert expected not in screen()
                        type_line(line)
                        if expected == 'AquaOS>':
                            time.sleep(2)
                        text = wait_for(expected)
                        assert 'task: pid=' not in text, text
                        time.sleep(.2)
                        return text

                    command('tcpcheck', 'TCP descriptor checks passed')
                    if no_nic:
                        command('httpget 6.6.6.6', 'connection failed (errno=19)')
                    else:
                        command(f'httpget 6.6.6.6 /ok {server.port}', 'AquaOS HTTP transfer PASS')
                        # Exercise more sockets than the table contains, including
                        # passive close and last-close cleanup between requests.
                        for _ in range(9):
                            command(f'httpget 6.6.6.6 /ok {server.port}', 'AquaOS HTTP transfer PASS')
                        command(f'httpget 6.6.6.6 /large {server.port} > /http.bin', 'AquaOS>')
                        command(f'httpget 6.6.6.6 /reset {server.port}', 'connection failed (errno=104)')
                        # A bound but non-listening host port reliably refuses.
                        command(f'httpget 6.6.6.6 / {server.refused_port}', 'connection failed (errno=111)')
                    qmp('quit')
            proc.wait(timeout=5)
            if not no_nic:
                saved = subprocess.check_output(['debugfs','-R','cat /http.bin',str(disk)],
                                                stderr=subprocess.DEVNULL)
                assert saved == response('/large'), (len(saved), len(response('/large')))
                assert server.paths == ['/ok']*10+['/large','/reset'], server.paths
                assert not server.error, server.error
            print('PASS: TCP no-NIC and descriptor lifetime' if no_nic else
                  'PASS: real HTTP, exact 20KB binary transfer, repeated close, reset, refusal and TCP descriptors')
        finally:
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


if __name__ == '__main__':
    with HttpServer() as server:
        main(server)
        main(server, no_nic=True)
