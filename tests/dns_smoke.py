#!/usr/bin/env python3
"""Deterministic userspace DNS over real E1000/ARP/UDP, without Internet DNS."""
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


def checksum(data):
    if len(data) % 2:
        data += b'\0'
    total = sum(struct.unpack('!' + 'H' * (len(data)//2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


class DnsPeer:
    """Small Ethernet fixture posing as the gateway and local DNS peer."""
    def __init__(self, connection):
        self.connection = connection
        connection.settimeout(.1)
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.error = None
        self.queries = {}
        self.arp_dns = False
        self.syns = 0
        self.dropped_data = False
        self.tcp_local = 0
        self.tcp_remote = 0xfffffff0
        self.request = b''
        self.responded = False
        self.pings = []

    def start(self):
        self.thread.start()

    def send(self, frame):
        frame += bytes(max(0, 60-len(frame)))
        self.connection.sendall(struct.pack('!I', len(frame)) + frame)

    def udp(self, incoming, payload, source_port=53):
        ip = incoming[14:]
        ihl = (ip[0] & 15)*4
        port, destination, length, check = struct.unpack('!HHHH', ip[ihl:ihl+8])
        assert destination == 53 and length == len(ip[ihl:])
        assert checksum(ip[:ihl]) == 0 and check
        pseudo = ip[12:20] + struct.pack('!BBH', 0, 17, length)
        assert checksum(pseudo+ip[ihl:]) == 0
        udp = struct.pack('!HHHH', source_port, port, len(payload)+8, 0) + payload
        addresses = ip[16:20] + ip[12:16]
        check = checksum(addresses+struct.pack('!BBH', 0,17,len(udp))+udp) or 65535
        udp = udp[:6]+struct.pack('!H',check)+udp[8:]
        header = struct.pack('!BBHHHBBH',0x45,0,len(udp)+20,0,0x4000,64,17,0)+addresses
        header = header[:10]+struct.pack('!H',checksum(header))+header[12:]
        # Match libslirp: DNS ARP advertises SOURCE, IP replies use GATEWAY.
        self.send(MAC+GATEWAY+b'\x08\x00'+header+udp)

    def icmp(self, frame):
        ip = frame[14:]
        total = int.from_bytes(ip[2:4], 'big')
        icmp = ip[(ip[0]&15)*4:total]
        assert checksum(ip[:20]) == 0 and checksum(icmp) == 0
        assert icmp[:2] == b'\x08\x00' and icmp[8:] == b'AquaOS ping'
        destination = ip[16:20]
        self.pings.append(destination)
        if destination == bytes([192,0,2,99]):
            return  # Deterministic no-reply destination.
        body = b'\0'+icmp[1:2]+b'\0\0'+icmp[4:]
        body = body[:2]+struct.pack('!H',checksum(body))+body[4:]
        header = struct.pack('!BBHHHBBH',0x45,0,20+len(body),0,0x4000,64,1,0)+ip[16:20]+ip[12:16]
        header = header[:10]+struct.pack('!H',checksum(header))+header[12:]
        mac = SOURCE if destination == bytes([6,6,6,3]) else GATEWAY
        self.send(MAC+mac+b'\x08\x00'+header+body)

    def tcp(self, frame):
        ip = frame[14:]
        total = int.from_bytes(ip[2:4], 'big')
        t = ip[(ip[0]&15)*4:total]
        port, destination, seq, ack, offset, flags, window, check, urgent = struct.unpack('!HHIIBBHHH',t[:20])
        assert destination == 80 and checksum(ip[:20]) == 0
        assert checksum(ip[12:20]+struct.pack('!BBH',0,6,len(t))+t) == 0
        data = t[(offset>>4)*4:]

        def send(seq, ack, flags, body=b'', damage=False):
            segment = struct.pack('!HHIIBBHHH',80,port,seq&0xffffffff,ack&0xffffffff,0x50,flags,4096,0,0)+body
            addresses = ip[16:20]+ip[12:16]
            check = checksum(addresses+struct.pack('!BBH',0,6,len(segment))+segment)
            segment = segment[:16]+struct.pack('!H',check ^ (1 if damage else 0))+segment[18:]
            header = struct.pack('!BBHHHBBH',0x45,0,20+len(segment),0,0x4000,64,6,0)+addresses
            header = header[:10]+struct.pack('!H',checksum(header))+header[12:]
            self.send(MAC+GATEWAY+b'\x08\x00'+header+segment)

        if flags & 2:
            self.syns += 1
            self.tcp_local = (seq+1)&0xffffffff
            if self.syns == 1:
                return  # Force SYN retransmission.
            send(self.tcp_remote,self.tcp_local,0x12)
        elif data:
            if not self.dropped_data:
                self.dropped_data = True
                return  # Force data retransmission.
            if seq != self.tcp_local:
                send(self.tcp_remote+1,self.tcp_local,0x10)
                return
            self.request += data
            self.tcp_local = (self.tcp_local+len(data))&0xffffffff
            send(self.tcp_remote+1,self.tcp_local,0x10)
            if b'\r\n\r\n' in self.request and not self.responded:
                assert b'Host: aqua.test:80\r\n' in self.request
                self.responded = True
                body = b'HTTP/1.0 200 OK\r\nConnection: close\r\n\r\nDNS HTTP integration PASS\n'
                start = self.tcp_remote+1
                send(start,self.tcp_local,0x18,b'CORRUPT',damage=True)
                send(start+20,self.tcp_local,0x18,body[20:])  # Out of order: retransmit below.
                send(start,self.tcp_local,0x18,body[:20])
                send(start+20,self.tcp_local,0x18,body[20:])
                self.tcp_remote = (start+len(body))&0xffffffff
                send(self.tcp_remote,self.tcp_local,0x11)
        elif flags & 1:
            self.tcp_local = (seq+1)&0xffffffff
            send(self.tcp_remote+1,self.tcp_local,0x10)

    def packet(self, frame):
        if frame[12:14] == b'\x08\x06' and frame[20:22] == b'\x00\x01':
            target = frame[38:42]
            assert target in (bytes([6,6,6,6]), bytes([6,6,6,3]))
            if target[-1] == 3:
                self.arp_dns = True
            mac = SOURCE if target[-1] == 3 else GATEWAY
            self.send(MAC+mac+bytes.fromhex('08060001080006040002')+
                      mac+target+MAC+bytes([6,6,6,15]))
        elif frame[12:14] == b'\x08\x00' and frame[23] == 1:
            self.icmp(frame)
        elif frame[12:14] == b'\x08\x00' and frame[23] == 6:
            self.tcp(frame)
        elif frame[12:14] == b'\x08\x00' and frame[23] == 17:
            if frame[34:38] == struct.pack('!HH', 68, 67):
                return  # This fixture supplies DNS, not DHCP.
            assert self.arp_dns and frame[:6] == SOURCE
            ip = frame[14:]
            total = int.from_bytes(ip[2:4], 'big')
            frame = frame[:14+total]
            assert ip[16:20] == bytes([6,6,6,3])
            data = frame[42:]
            at, labels = 12, []
            while data[at]:
                n = data[at];labels.append(data[at+1:at+1+n].decode());at += n+1
            name = '.'.join(labels)
            assert data[at:] == b'\0\0\1\0\1'
            self.queries[name] = self.queries.get(name, 0)+1
            if name == 'silent.test' or (name == 'aqua.test' and self.queries[name] == 1):
                return
            flags = 0x8183 if name == 'missing.test' else 0x8380 if name == 'truncated.test' else 0x8182 if name == 'broken.test' else 0x8180
            good = name in ('aqua.test', 'again.test')
            header = data[:2] + struct.pack('!HHHHH',flags,1,2 if good else 0,0,0)
            response = header+data[12:]
            if good:
                # CNAME alias.test uses compressed 'test' from the question.
                suffix = 13+len(labels[0])
                alias_at = len(response)+12
                response += b'\xc0\x0c'+struct.pack('!HHIH',5,1,60,8)
                response += b'\x05alias'+struct.pack('!H',0xc000|suffix)
                response += struct.pack('!H',0xc000|alias_at)+struct.pack('!HHIH',1,1,60,4)
                response += bytes([192,0,2,42 if name == 'aqua.test' else 43])
                self.udp(frame, response, source_port=54)
                self.udp(frame, bytes([response[0]^1])+response[1:])
                malformed = response[:12]+b'\xff'+response[13:]
                self.udp(frame, malformed)
            self.udp(frame,response)

    def serve(self):
        pending = b''
        try:
            while not self.stop.is_set():
                try:
                    data = self.connection.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    break
                pending += data
                while len(pending) >= 4:
                    size = int.from_bytes(pending[:4], 'big')
                    if len(pending) < size+4:
                        break
                    frame, pending = pending[4:4+size], pending[4+size:]
                    self.packet(frame)
        except Exception as error:
            self.error = error


def main(no_nic=False):
    with tempfile.TemporaryDirectory(prefix='aquaos-dns-') as folder:
        tmp = Path(folder)
        disk = tmp / 'disk.img'
        with disk.open('wb') as stream:
            stream.truncate(32 * 1024 * 1024)
        subprocess.run(['mkfs.ext2', '-q', '-F', '-b', '4096', str(disk)], check=True)
        batch = tmp / 'install.txt'
        batch.write_text(f'mkdir /bin\nwrite {ROOT}/build/x86_64/user/shell /bin/shell\nwrite {ROOT}/build/x86_64/user/nslookup /bin/nslookup\nwrite {ROOT}/build/x86_64/user/httpget /bin/httpget\nwrite {ROOT}/build/x86_64/user/ping /bin/ping\nwrite {ROOT}/build/x86_64/user/netinfo /bin/netinfo\n')
        subprocess.run(['debugfs', '-w', '-f', str(batch), str(disk)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        host, guest = socket.socketpair()
        host.settimeout(5)
        path = tmp / 'qmp'
        proc = subprocess.Popen([
            'qemu-system-x86_64', '-m', '128M', '-display', 'none', '-serial', 'none',
            '-no-reboot', '-cdrom', str(ROOT / 'build/x86_64/aquaos.iso'),
            '-drive', f'file={disk},format=raw,if=ide,snapshot=on',
            *(['-nic', 'none'] if no_nic else ['-netdev', f'socket,id=n0,fd={guest.fileno()}',
            '-device', 'e1000,netdev=n0,mac=52:54:00:12:34:56']),
            '-qmp', f'unix:{path},server=on,wait=off'],
            pass_fds=(guest.fileno(),), stderr=subprocess.PIPE)
        guest.close()
        peer = DnsPeer(host)
        peer.start()
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
                            key = {' ': 'spc', '.': 'dot', '-': 'minus'}.get(c, c)
                            qmp('human-monitor-command', {'command-line': 'sendkey ' + key})
                            time.sleep(.06)
                        qmp('human-monitor-command', {'command-line': 'sendkey ret'})

                    def command(line, expected):
                        # Remove previous results so identical failures cannot
                        # satisfy the next command's assertion prematurely.
                        type_line('clear')
                        time.sleep(.2)
                        assert expected not in screen()
                        type_line(line)
                        text = wait_for(expected)
                        assert 'task: pid=' not in text, text
                        time.sleep(.2)
                        return text

                    if no_nic:
                        command('nslookup aqua.test', 'lookup failed (errno=19)')
                    else:
                        command('nslookup aqua.test', 'Address: 192.0.2.42')
                        command('nslookup missing.test', 'name not found or no IPv4 answer')
                        command('nslookup truncated.test', 'truncated or overly complex DNS response')
                        command('nslookup broken.test', 'DNS server failure')
                        command('nslookup silent.test', 'timed out')
                        command('netinfo', 'DHCP: static fallback')
                        # More than eight sequential sockets verifies cleanup on failure.
                        for _ in range(9):
                            command('nslookup missing.test', 'name not found or no IPv4 answer')
                        command('nslookup again.test', 'Address: 192.0.2.43')
                        assert peer.arp_dns and peer.queries.get('aqua.test') == 2, peer.queries
                        assert peer.queries.get('silent.test') == 3, peer.queries
                        assert peer.queries.get('missing.test') == 10, peer.queries
                        command('httpget aqua.test', 'DNS HTTP integration PASS')
                        assert peer.syns == 2 and peer.dropped_data and peer.responded
                        assert 'CORRUPT' not in screen()
                        command('ping -c 2 aqua.test', '2 transmitted, 2 received')
                        command('ping -c 1 6.6.6.3', '1 transmitted, 1 received')
                        command('ping -c 1 192.0.2.99', '1 transmitted, 0 received')
                        command('ping 999.1.1.1', 'usage: ping')
                        type_line('ping -c 100 aqua.test')
                        wait_for('PING aqua.test: 100 requests')
                        qmp('human-monitor-command', {'command-line': 'sendkey ctrl-c'})
                        time.sleep(.3)
                        command('ping -c 1 6.6.6.3', '1 transmitted, 1 received')
                        assert peer.pings.count(bytes([192,0,2,42])) >= 2
                        assert peer.pings.count(bytes([6,6,6,3])) == 2
                    assert not peer.error, peer.error
                    print('PASS: DNS no-NIC error' if no_nic else
                          'PASS: DNS, HTTP and hostname/local/remote ping, timeout, Ctrl+C and routing')
        finally:
            peer.stop.set()
            peer.thread.join(timeout=2)
            host.close()
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


if __name__ == '__main__':
    main()
    main(no_nic=True)
