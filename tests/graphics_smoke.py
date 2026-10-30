#!/usr/bin/env python3
"""Validate framebuffer pixels, user display ABI and BMP viewer in QEMU.
Run sh build.sh && python3 tools/iso64.py first. Uses a disposable disk.
"""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from console_address import console_address

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from bmp_demo import demo_bmp


def run():
    with tempfile.TemporaryDirectory(prefix='aquaos-graphics-') as directory:
        tmp = Path(directory)
        disk = tmp/'disk.img'
        with disk.open('wb') as f:
            f.truncate(64*1024*1024)
        subprocess.run(['mkfs.ext2', '-q', '-F', '-b', '4096', str(disk)], check=True)
        commands = ['mkdir /bin']
        for name in ('shell', 'bmpview', 'displaycheck', 'echo', 'cat', 'kilo'):
            commands.append(f'write {ROOT}/build/x86_64/user/{name} /bin/{name}')
        for name, data in [('demo.bmp', demo_bmp()), ('top.bmp', demo_bmp(True)),
                           ('bad.bmp', demo_bmp()[:60])]:
            (tmp/name).write_bytes(data)
            commands.append(f'write {tmp/name} /{name}')
        (tmp/'history.txt').write_text(''.join(f'history line {i:04d}\n' for i in range(1100)))
        commands.append(f'write {tmp}/history.txt /history.txt')
        (tmp/'install').write_text('\n'.join(commands)+'\n')
        subprocess.run(['debugfs', '-w', '-f', str(tmp/'install'), str(disk)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sockpath = tmp/'qmp'
        proc = subprocess.Popen(['qemu-system-x86_64', '-m', '128M', '-display', 'none',
                                 '-no-reboot', '-cdrom', str(ROOT/'build/x86_64/aquaos.iso'),
                                 '-drive', f'file={disk},format=raw,if=ide',
                                 '-qmp', f'unix:{sockpath},server=on,wait=off'],
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic()+10
            while not sockpath.exists():
                if proc.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError('QEMU did not start')
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(15)
                sock.connect(str(sockpath))
                stream = sock.makefile('rwb', buffering=0)
                stream.readline()

                def qmp(name, arguments=None):
                    stream.write((json.dumps({'execute': name, 'arguments': arguments or {}})+'\n').encode())
                    while True:
                        reply = json.loads(stream.readline())
                        if 'error' in reply:
                            raise RuntimeError(reply)
                        if 'return' in reply:
                            return reply['return']

                def key(name):
                    qmp('human-monitor-command', {'command-line': f'sendkey {name} 10'})
                    time.sleep(.035)

                def screen(address=None):
                    target = tmp/'console.bin'
                    qmp('human-monitor-command', {'command-line': f'pmemsave {address or console_address()} 4000 "{target}"'})
                    raw = target.read_bytes()[::2]
                    return '\n'.join(raw[i:i+80].decode('ascii', errors='replace').rstrip() for i in range(0, 2000, 80))

                def wait(text, address=None):
                    deadline = time.monotonic()+15
                    while time.monotonic()<deadline:
                        current = screen(address)
                        if 'FAIL:' in current or 'fatal kernel exception' in current:
                            raise AssertionError(current)
                        if text in current:
                            return current
                        time.sleep(.1)
                    raise AssertionError(f'Missing {text!r}:\n{current}')

                def command(text, expected):
                    keys = {' ': 'spc', '/': 'slash', '.': 'dot', '-': 'minus'}
                    for c in text:
                        key(keys.get(c, c))
                    key('ret')
                    return wait(expected)

                def screenshot():
                    target = tmp/'screen.ppm'
                    qmp('screendump', {'filename': str(target)})
                    magic, dimensions, maxval, data = target.read_bytes().split(b'\n', 3)
                    assert magic == b'P6' and maxval == b'255'
                    w, h = map(int, dimensions.split())
                    assert len(data) == w*h*3
                    return w, h, data

                def assert_card():
                    deadline = time.monotonic()+5
                    while time.monotonic()<deadline:
                        w, h, data = screenshot()
                        x, y = (w-160)//2, (h-120)//2
                        def pixel(px, py):
                            offset = (py*w+px)*3
                            return tuple(data[offset:offset+3])
                        corners = [pixel(x+10,y+10), pixel(x+150,y+10),
                                   pixel(x+10,y+110), pixel(x+150,y+110)]
                        if corners == [(255,0,0), (0,255,0), (0,0,255), (255,255,255)]:
                            assert pixel(0,0) == (0,0,0)
                            return
                        time.sleep(.1)
                    raise AssertionError(f'Unexpected BMP pixels: {corners}')

                def assert_terminal_pixels():
                    deadline = time.monotonic()+5
                    while time.monotonic()<deadline:
                        _, _, data = screenshot()
                        # The test card has pure red/green/blue pixels. The text
                        # console uses VGA palette colors, none of these three.
                        card_colors = {(255,0,0), (0,255,0), (0,0,255)}
                        seen = {tuple(data[i:i+3]) for i in range(0,len(data),3)}
                        if any(data) and not (seen & card_colors):
                            return
                        time.sleep(.1)
                    raise AssertionError('Terminal pixel repaint did not replace the image')

                qmp('qmp_capabilities')
                wait('GNU GRUB', '0xb8000')
                key('ret')
                wait('shell at')
                command('displaycheck', 'PASS: display ABI')
                command('bmpview /bad.bmp', 'cannot load 24-bit BMP')
                for name in ('demo.bmp', 'top.bmp'):
                    command(f'bmpview /{name}', 'press any key to return')
                    assert_card()
                    key('q')
                    # Text mirror may already contain an earlier prompt; allow repaint.
                    time.sleep(.25)
                    assert_terminal_pixels()
                    command('echo restored', '\nrestored\n')
                command('bmpview /demo.bmp', 'press any key to return')
                assert_card()
                key('ctrl-c')
                time.sleep(.25)
                command('echo interrupted', '\ninterrupted\n')
                assert_terminal_pixels()
                command('cat /history.txt', 'history line 1099')
                live = screen()
                key('pgup'); time.sleep(.2)
                assert screen()!=live and 'history line' in screen()
                key('pgdn'); time.sleep(.2)
                assert screen()==live
                # Save an inspectable screenshot outside the disposable test directory.
                out = ROOT/'build/x86_64/graphics-check'
                out.mkdir(parents=True, exist_ok=True)
                command('bmpview /demo.bmp', 'press any key to return')
                assert_card()
                qmp('screendump', {'filename': str(out/'demo.ppm')})
                key('q')
                assert_terminal_pixels()
                qmp('screendump', {'filename': str(out/'terminal.ppm')})
                qmp('quit')
                proc.wait(timeout=5)
            subprocess.run(['e2fsck', '-fn', str(disk)], check=True, stdout=subprocess.DEVNULL)
            print('PASS: display ABI, BMP pixels/orientation, key/Ctrl+C restore and scrollback')
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)


if __name__ == '__main__':
    run()
