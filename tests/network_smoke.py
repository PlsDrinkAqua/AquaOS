#!/usr/bin/env python3
"""Check PCI discovery and the QEMU network harness using a disposable disk."""
import json
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def boot(tmp, disk, label, devices, expected):
    monitor = tmp / (label + '.qmp')
    capture = tmp / (label + '.pcap')
    network = ['-netdev', 'user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dhcpstart=10.0.2.15',
               '-object', f'filter-dump,id=capture,netdev=net0,file={capture}'] if devices else ['-nic', 'none']
    command = ['qemu-system-x86_64', '-m', '128M', '-display', 'none', '-serial', 'none',
               '-no-reboot', '-cdrom', str(ROOT / 'build/x86_64/myos.iso'),
               '-drive', f'file={disk},format=raw,if=ide,snapshot=on',
               '-qmp', f'unix:{monitor},server=on,wait=off', *network, *devices]
    with subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE) as proc:
        try:
            deadline = time.monotonic() + 20
            while not monitor.exists():
                if proc.poll() is not None:
                    raise RuntimeError(proc.stderr.read().decode())
                if time.monotonic() > deadline:
                    raise TimeoutError('QMP startup')
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as connection:
                connection.settimeout(5)
                connection.connect(str(monitor))
                with connection.makefile('rwb', buffering=0) as stream:
                    json.loads(stream.readline())

                    def qmp(name, arguments=None):
                        stream.write((json.dumps({'execute': name, 'arguments': arguments or {}}) + '\n').encode())
                        while True:
                            reply = json.loads(stream.readline())
                            if 'error' in reply:
                                raise RuntimeError(reply)
                            if 'return' in reply:
                                return reply['return']

                    qmp('qmp_capabilities')
                    qmp('human-monitor-command', {'command-line': 'sendkey ret'})
                    screen = ''
                    while time.monotonic() < deadline:
                        dump = tmp / 'vga.bin'
                        qmp('human-monitor-command', {'command-line': f'pmemsave 0xb8000 4000 "{dump}"'})
                        data = dump.read_bytes()[::2]
                        screen = '\n'.join(data[i:i+80].decode('ascii', errors='replace') for i in range(0, 2000, 80))
                        if 'GNU GRUB' in screen:
                            qmp('human-monitor-command', {'command-line': 'sendkey ret'})
                        if 'fatal kernel exception' in screen:
                            raise AssertionError(screen)
                        if 'shell at' in screen and expected in screen:
                            break
                        time.sleep(.1)
                    else:
                        raise AssertionError(f'{label}: missing {expected!r}\n{screen}')
                    if devices:
                        match = re.search(r'E1000 MMIO=0x([0-9a-f]+) IRQ=(\d+) pin=(\d+) command=0x([0-9a-f]+)', screen)
                        assert match, screen
                        address, irq, pin, command_bits = (int(match[1], 16), int(match[2]), int(match[3]), int(match[4], 16))
                        assert address and 0 < irq < 16 and pin == 1 and command_bits & 7 == 7, screen
                        assert 'BAR0 MMIO' in screen and 'BAR1 IO' in screen, screen
                        # Compare decoded guest BAR/IRQ against QEMU's device inventory.
                        def find_nic(buses):
                            for bus in buses:
                                for dev in bus.get('devices', []):
                                    if dev.get('id', {}).get('vendor') == 0x8086 and dev['id'].get('device') == 0x100e:
                                        return dev
                                    bridge = dev.get('pci_bridge')
                                    if bridge:
                                        result = find_nic([{'devices': bridge.get('devices', [])}])
                                        if result:
                                            return result
                            return None
                        inventory = qmp('query-pci')
                        nic = find_nic(inventory)
                        assert nic, inventory
                        bar0 = next(region for region in nic['regions'] if region['bar'] == 0)
                        assert address == bar0['address'] and irq == nic['irq'], (match.groups(), nic)
                    qmp('quit')
            proc.wait(timeout=5)
            if devices:
                header = capture.read_bytes()[:24]
                assert len(header) == 24, 'PCAP header missing'
                endian = '<' if header[:4] == b'\xd4\xc3\xb2\xa1' else '>'
                magic, major, minor, _, _, snaplen, linktype = struct.unpack(endian + 'IHHIIII', header)
                assert magic == 0xa1b2c3d4 and (major, minor) == (2, 4) and snaplen and linktype == 1, header
            print(f'PASS: {label}')
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)


def main():
    with tempfile.TemporaryDirectory(prefix='zenos-network-') as directory:
        tmp = Path(directory)
        disk = tmp / 'disk.img'
        with disk.open('wb') as output:
            output.truncate(32 * 1024 * 1024)
        subprocess.run(['mkfs.ext2', '-q', '-F', '-b', '4096', str(disk)], check=True)
        batch = tmp / 'install.txt'
        batch.write_text(f'mkdir /bin\nwrite {ROOT / "build/x86_64/user/shell"} /bin/shell\n')
        subprocess.run(['debugfs', '-w', '-f', str(batch), str(disk)], check=True, capture_output=True)
        boot(tmp, disk, 'e1000', ['-device', 'e1000,netdev=net0,mac=52:54:00:12:34:56'], '8086:100e class=2:0')
        boot(tmp, disk, 'bridge', ['-device', 'pci-bridge,id=bridge,chassis_nr=1', '-device',
                                 'e1000,bus=bridge,netdev=net0,mac=52:54:00:12:34:56'], 'PCI 1:')
        boot(tmp, disk, 'multifunction', ['-device', 'virtio-balloon-pci,addr=4.0,multifunction=on',
                                        '-device', 'e1000,addr=4.1,netdev=net0,mac=52:54:00:12:34:56'],
             'PCI 0:4.1 8086:100e')
        boot(tmp, disk, 'no-nic', [], 'PCI: no supported E1000 network card')


if __name__ == '__main__':
    main()
