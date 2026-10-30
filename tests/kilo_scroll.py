#!/usr/bin/env python3
"""Scroll real tcc.h in QEMU; check source rows and framebuffer glyph pixels.
Build kernel/userspace/ISO and stage TinyCC first. Uses a disposable disk.
"""
import json
from pathlib import Path
import re
import socket
import subprocess
import struct
import tempfile
import time
from console_address import console_address
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/x86_64'
PALETTE=[0x000000,0x0000aa,0x00aa00,0x00aaaa,0xaa0000,0xaa00aa,0xaa5500,0xaaaaaa,
         0x555555,0x5555ff,0x55ff55,0x55ffff,0xff5555,0xff55ff,0xffff55,0xffffff]
FONT=[int(x,16) for x in re.findall(r'0x[0-9a-f]+',(ROOT/'kernel/kernel/TTY/font8x16.h').read_text())]

def render(line):
    result=''
    for c in line:
        result+=' ' if c=='\t' else c
        if c=='\t':
            while (len(result)+1)%8: result+=' '
    return result[:79].ljust(80)

def run():
    source=OUT/'root/home/user/tinycc/tcc.h'
    rows=source.read_text().splitlines()
    symbols=subprocess.check_output(['nm','-n',str(OUT/'aquaos.kernel')],text=True)
    cursor_address=next(hex(int(line.split()[0],16)-0xffffffff80000000)
                        for line in symbols.splitlines() if line.split()[-1]=='terminal_row')
    with tempfile.TemporaryDirectory(prefix='aquaos-kilo-scroll-') as directory:
        tmp=Path(directory); disk=tmp/'disk.img'
        with disk.open('wb') as f: f.truncate(64*1024*1024)
        subprocess.run(['mkfs.ext2','-q','-F','-b','4096',str(disk)],check=True)
        (tmp/'install').write_text(f'mkdir /bin\nwrite {OUT}/user/shell /bin/shell\nwrite {OUT}/user/kilo /bin/kilo\nwrite {source} /tcc.h\n')
        subprocess.run(['debugfs','-w','-f',str(tmp/'install'),str(disk)],check=True,capture_output=True)
        proc=subprocess.Popen(['qemu-system-x86_64','-m','128M','-display','none','-no-reboot',
                               '-cdrom',str(OUT/'aquaos.iso'),'-drive',f'file={disk},format=raw,if=ide',
                               '-qmp',f'unix:{tmp}/qmp,server=on,wait=off'],stderr=subprocess.PIPE)
        try:
            deadline=time.monotonic()+10
            while not (tmp/'qmp').exists():
                if time.monotonic()>deadline: raise TimeoutError('QMP')
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(15); sock.connect(str(tmp/'qmp'))
                stream=sock.makefile('rwb',buffering=0); stream.readline()
                def qmp(name,args=None):
                    stream.write((json.dumps({'execute':name,'arguments':args or {}})+'\n').encode())
                    while True:
                        r=json.loads(stream.readline())
                        if 'error' in r: raise RuntimeError(r)
                        if 'return' in r: return r['return']
                def key(name,delay=.03):
                    qmp('human-monitor-command',{'command-line':f'sendkey {name} 1'})
                    time.sleep(delay)
                def screen(address=None):
                    qmp('human-monitor-command',{'command-line':f'pmemsave {address or console_address()} 4000 "{tmp}/cells"'})
                    raw=(tmp/'cells').read_bytes()
                    return raw,[''.join(chr(raw[2*(y*80+x)]) for x in range(80)) for y in range(25)]
                def wait(text,address=None):
                    deadline=time.monotonic()+15
                    while time.monotonic()<deadline:
                        _,lines=screen(address); current='\n'.join(lines)
                        if 'fatal kernel exception' in current: raise AssertionError(current)
                        if text in current: return
                        time.sleep(.05)
                    raise AssertionError(current)
                def snapshot():
                    # Settle the last key, then freeze both the character model
                    # and physical pixels for an independent raster comparison.
                    time.sleep(.15); qmp('stop')
                    try:
                        cells,lines=screen()
                        match=re.search(r'(\d+)/'+str(len(rows)),lines[23]); assert match,lines
                        current=int(match[1])
                        qmp('human-monitor-command',{'command-line':f'pmemsave {cursor_address} 8 "{tmp}/cursor"'})
                        cursor_row=struct.unpack('<Q',(tmp/'cursor').read_bytes())[0]
                        first=current-1-cursor_row
                        assert first>=0 and cursor_row<23
                        for y in range(23):
                            assert lines[y]==render(rows[first+y]),(current,y,lines[y],render(rows[first+y]))
                        target=tmp/'frame.ppm'
                        qmp('screendump',{'filename':str(target)})
                        magic,dimensions,maximum,pixels=target.read_bytes().split(b'\n',3)
                        assert magic==b'P6' and maximum==b'255'
                        w,h=map(int,dimensions.split()); left=(w-640)//2; top=(h-400)//2
                        for y in range(25):
                            for gy in range(16):
                                expected=bytearray()
                                for x in range(80):
                                    ch,color=cells[2*(y*80+x):2*(y*80+x)+2]
                                    bits=FONT[(ch if ch<128 else ord('?'))*16+gy]
                                    fg=PALETTE[color&15]; bg=PALETTE[(color>>4)&7]
                                    for gx in range(8):
                                        rgb=fg if bits&(0x80>>gx) else bg
                                        expected.extend((rgb>>16,(rgb>>8)&255,rgb&255))
                                offset=((top+y*16+gy)*w+left)*3
                                assert pixels[offset:offset+640*3]==expected,(current,y,gy)
                        return current
                    finally: qmp('cont')
                qmp('qmp_capabilities'); wait('GNU GRUB','0xb8000'); key('ret'); wait('shell at')
                for c in 'kilo /tcc.h': key({' ':'spc','/':'slash','.':'dot'}.get(c,c))
                key('ret'); wait('Ctrl-S = save'); assert snapshot()==1
                for block in range(30):
                    for _ in range(50): key('down',.015)
                    wait(f'{(block+1)*50+1}/{len(rows)}')
                    assert snapshot()==(block+1)*50+1
                # A burst queues new input while earlier frames are being drawn.
                for _ in range(80): key('down',.003)
                time.sleep(3)
                assert snapshot()>1501
                for _ in range(30): key('up')
                assert snapshot()>23
                key('ctrl-q'); wait('AquaOS>'); qmp('quit'); proc.wait(timeout=5)
            print('PASS: tcc.h continuous/burst scrolling, source rows and framebuffer pixels')
        finally:
            if proc.poll() is None: proc.terminate(); proc.wait(timeout=5)

if __name__=='__main__': run()
