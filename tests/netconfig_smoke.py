#!/usr/bin/env python3
"""DHCP/netinfo through real QEMU user networking; optional live DNS check.

Build kernel, userspace and ISO first. Uses disposable disks, no existing disks.
The default tests need no Internet. --live-dns additionally resolves google.com
and preserves the guest output and PCAP under build/x86_64/network-live/.
"""
from console_address import console_address
import argparse
import http.server
import ipaddress
import json
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


class Guest:
    def __init__(self, folder, network):
        self.tmp = Path(folder)
        self.control = self.stream = self.proc = None
        disk = self.tmp / 'disk.img'
        with disk.open('wb') as f:
            f.truncate(32 * 1024 * 1024)
        subprocess.run(['mkfs.ext2', '-q', '-F', '-b', '4096', str(disk)], check=True)
        batch = self.tmp / 'install.txt'
        batch.write_text('mkdir /bin\n' + ''.join(
            f'write {ROOT}/build/x86_64/user/{name} /bin/{name}\n'
            for name in ['shell', 'netinfo', 'nslookup', 'httpget', 'ping']))
        subprocess.run(['debugfs', '-w', '-f', str(batch), str(disk)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        path = self.tmp / 'qmp'
        self.capture = self.tmp / 'network.pcap'
        nic = ['-nic', 'none'] if network is None else [
            '-netdev', 'user,id=n0,' + network,
            '-device', 'e1000,netdev=n0,mac=52:54:00:12:34:56',
            '-object', f'filter-dump,id=capture,netdev=n0,file={self.capture}']
        try:
            self.proc = subprocess.Popen([
                'qemu-system-x86_64', '-m', '128M', '-display', 'none', '-serial', 'none',
                '-no-reboot', '-cdrom', str(ROOT/'build/x86_64/aquaos.iso'),
                '-drive', f'file={disk},format=raw,if=ide',
                '-qmp', f'unix:{path},server=on,wait=off', *nic], stderr=subprocess.PIPE)
            deadline = time.monotonic() + 15
            while not path.exists():
                if self.proc.poll() is not None:
                    raise RuntimeError(self.proc.stderr.read().decode())
                if time.monotonic() > deadline:
                    raise TimeoutError('QMP startup')
                time.sleep(.05)
            self.control = socket.socket(socket.AF_UNIX)
            self.control.settimeout(10)
            self.control.connect(str(path))
            self.stream = self.control.makefile('rwb', buffering=0)
            self.stream.readline()
            self.qmp('qmp_capabilities')
            time.sleep(1)
            self.monitor('sendkey ret')
            self.wait('shell at')
        except BaseException:
            self.close()
            raise

    def qmp(self, name, arguments=None):
        self.stream.write((json.dumps({'execute': name, 'arguments': arguments or {}})+'\n').encode())
        while True:
            reply = json.loads(self.stream.readline())
            if 'error' in reply:
                raise RuntimeError(reply)
            if 'return' in reply:
                return reply['return']

    def monitor(self, command):
        return self.qmp('human-monitor-command', {'command-line': command})

    def screen(self):
        dump = self.tmp/'vga.bin'
        self.monitor(f'pmemsave {console_address()} 4000 "{dump}"')
        data = dump.read_bytes()[::2]
        return '\n'.join(data[i:i+80].decode('ascii', errors='replace') for i in range(0,2000,80))

    def wait(self, expected, timeout=25):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.screen()
            if 'fatal kernel exception' in text:
                raise AssertionError(text)
            if expected in text:
                return text
            time.sleep(.05)
        raise AssertionError(f'Missing {expected}:\n{text}')

    def type(self, line):
        for c in line:
            key = {' ': 'spc', '.': 'dot', '-': 'minus', '/': 'slash'}.get(c,c)
            self.monitor('sendkey ' + key)
            time.sleep(.04)
        self.monitor('sendkey ret')
        time.sleep(.15)

    def command(self, line, expected):
        self.type('clear')
        self.type(line)
        text = self.wait(expected)
        time.sleep(.15)
        return text

    def close(self):
        if self.stream:
            self.stream.close()
        if self.control:
            self.control.close()
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
            self.proc.stderr.close()


def frames(path):
    data = path.read_bytes()
    endian = '<' if data[:4] == bytes.fromhex('d4c3b2a1') else '>'
    at = 24
    while at < len(data):
        _, _, length, original = struct.unpack_from(endian+'IIII',data,at)
        assert length == original and at+16+length <= len(data)
        yield data[at+16:at+16+length]
        at += 16+length


class HttpPeer(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'DHCP routing and TCP checksum PASS\n'
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *unused):
        pass


def main(live=False):
    server = http.server.ThreadingHTTPServer(('127.0.0.1',0),HttpPeer)
    thread = threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    try:
        for label, network, ip, gateway, dns in [
            ('default', 'net=6.6.6.0/24,host=6.6.6.6,dhcpstart=6.6.6.15', '6.6.6.15', '6.6.6.6', '6.6.6.3'),
            ('changed', 'net=10.20.30.0/24,host=10.20.30.1,dhcpstart=10.20.30.50,dns=10.20.30.9', '10.20.30.50', '10.20.30.1', '10.20.30.9'),
            ('no-nic', None, '', '', '')]:
            with tempfile.TemporaryDirectory(prefix='aquaos-dhcp-') as folder:
                guest = Guest(folder, network)
                try:
                    time.sleep(2)
                    text = guest.command('netinfo', 'Network: no interface' if network is None else 'DHCP: bound')
                    if network is None:
                        guest.command('nslookup google.com', 'lookup failed (errno=19)')
                    else:
                        for expected in [f'IPv4: {ip}', f'Gateway: {gateway}', f'DNS: {dns}', 'Link: up', 'Netmask: 255.255.255.0', 'Lease:']:
                            assert expected in text, text
                        guest.command(f'httpget {gateway} / {server.server_port}', 'DHCP routing and TCP checksum PASS')
                        guest.command('ping', '4 transmitted, 4 received')
                        # Link reports the current E1000 hardware status.
                        guest.qmp('set_link', {'name': 'n0', 'up': False})
                        guest.command('netinfo', 'Link: down')
                        guest.qmp('set_link', {'name': 'n0', 'up': True})
                        guest.command('netinfo', 'Link: up')
                        if live:
                            text = guest.command('nslookup google.com', 'Address:')
                            address = re.search(r'Address: ([0-9.]+)',text).group(1)
                            kind = 'public' if ipaddress.ip_address(address).is_global else 'non-public/synthetic'
                            print(f'LIVE {label}: google.com -> {address} via {dns} ({kind})', flush=True)
                            out = ROOT/'build/x86_64/network-live'
                            out.mkdir(exist_ok=True)
                            (out/f'{label}.txt').write_text(text+'\n')
                finally:
                    guest.close()
                if network:
                    captured = list(frames(guest.capture))
                    dhcp_types = [f[284] for f in captured if len(f)>=285 and f[23]==17 and f[34:38]==struct.pack('!HH',68,67)]
                    assert 1 in dhcp_types and 3 in dhcp_types, dhcp_types
                    if live:
                        import shutil
                        shutil.copyfile(guest.capture,out/f'{label}.pcap')
                        # Ensure the printed address was actually supplied by a
                        # port-53 reply from the configured DNS proxy.
                        packed = socket.inet_aton(address)
                        assert any(f[12:14]==b'\x08\x00' and f[23]==17 and
                                   f[26:30]==socket.inet_aton(dns) and f[34:36]==b'\x00\x35' and
                                   packed in f[42:] for f in captured)
                print(f'PASS: DHCP/netinfo {label}', flush=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--live-dns', action='store_true')
    main(parser.parse_args().live_dns)
