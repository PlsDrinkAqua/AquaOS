#!/usr/bin/env python3
"""Build/run an instrumented Kilo first-frame benchmark on a disposable QEMU disk.
Requires the normal built kernel, libc and ISO. --label retains each binary and
JSON measurements under build/x86_64/kilo-profile. Timings use QEMU TSC ticks;
host_seconds includes process startup, first render and reporting, not typing.
"""
import argparse
import json
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
from console_address import console_address
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build64 as build


def run(label,repeats,build_only=False,reuse_binary=False):
    out=build.OUT/'kilo-profile'
    out.mkdir(parents=True,exist_ok=True)
    binary=out/(label+'.elf')
    obj=None if reuse_binary else build.compile_file('tests/kilo_profile.c',build.UFLAGS)
    if not reuse_binary: build.run([*build.CC,*build.FLAGS,'-nostdlib','-no-pie','-Wl,--build-id=none',
               '-Wl,--wrap=read,--wrap=write,--wrap=malloc,--wrap=realloc',
               '-T',ROOT/'user/user.ld','-o',binary,
               build.OUT/'obj/userlibc/arch/x86_64/crt0.o',obj,build.OUT/'libc.a','-lgcc'])
    if build_only: return
    fixture=build.OUT/'root/home/user/tinycc/tcc.h'
    if not fixture.exists():
        raise RuntimeError('Build the TinyCC port first (tcc.h fixture is missing)')
    cache_address=None
    for line in subprocess.check_output(['nm','-n',str(build.OUT/'aquaos.kernel')],text=True).splitlines():
        fields=line.split()
        if len(fields)==3 and fields[2]=='disk_cache_stats':
            cache_address=hex(int(fields[0],16)-0xffffffff80000000)
    results=[]
    with tempfile.TemporaryDirectory(prefix='aquaos-kilo-profile-') as directory:
        tmp=Path(directory); disk=tmp/'disk.img'
        with disk.open('wb') as f: f.truncate(64*1024*1024)
        subprocess.run(['mkfs.ext2','-q','-F','-b','4096',str(disk)],check=True)
        install=tmp/'install'
        install.write_text(f'mkdir /bin\nwrite {build.OUT}/user/shell /bin/shell\nwrite {binary} /bin/kiloprof\nwrite {fixture} /tcc.h\n')
        subprocess.run(['debugfs','-w','-f',str(install),str(disk)],check=True,capture_output=True)
        proc=subprocess.Popen(['qemu-system-x86_64','-m','128M','-display','none','-no-reboot',
                               '-cdrom',str(build.OUT/'aquaos.iso'),'-drive',f'file={disk},format=raw,if=ide',
                               '-qmp',f'unix:{tmp}/qmp,server=on,wait=off'],stderr=subprocess.PIPE)
        try:
            deadline=time.monotonic()+10
            while not (tmp/'qmp').exists():
                if time.monotonic()>deadline: raise TimeoutError('QMP')
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(30); sock.connect(str(tmp/'qmp'))
                stream=sock.makefile('rwb',buffering=0); stream.readline()
                def qmp(cmd,args=None):
                    stream.write((json.dumps({'execute':cmd,'arguments':args or {}})+'\n').encode())
                    while True:
                        reply=json.loads(stream.readline())
                        if 'error' in reply: raise RuntimeError(reply)
                        if 'return' in reply: return reply['return']
                def key(k):
                    qmp('human-monitor-command',{'command-line':f'sendkey {k} 10'})
                    time.sleep(.035)
                def screen(address=None):
                    qmp('human-monitor-command',{'command-line':f'pmemsave {address or console_address()} 4000 "{tmp}/screen"'})
                    data=(tmp/'screen').read_bytes()[::2]
                    return '\n'.join(data[i:i+80].decode('ascii',errors='replace').rstrip() for i in range(0,2000,80))
                def wait(text,address=None,timeout=180):
                    deadline=time.monotonic()+timeout
                    while time.monotonic()<deadline:
                        current=screen(address)
                        if 'fatal kernel exception' in current: raise AssertionError(current)
                        if text in current: return current
                        time.sleep(.05)
                    raise TimeoutError(current)
                def cache_stats():
                    if cache_address is None: return {}
                    target=tmp/'cache-stats'
                    qmp('human-monitor-command',{'command-line':f'pmemsave {cache_address} 40 "{target}"'})
                    return dict(zip(('hits','misses','read_sectors','write_sectors','evictions'),
                                    struct.unpack('<5Q',target.read_bytes())))
                qmp('qmp_capabilities'); wait('GNU GRUB','0xb8000',10); key('ret'); wait('shell at')
                for trial in range(repeats):
                    # Clear removes the previous report, so completion cannot be stale.
                    for c in 'clear': key(c)
                    key('ret'); time.sleep(.15)
                    for c in 'kiloprof /tcc.h': key({' ':'spc','/':'slash','.':'dot'}.get(c,c))
                    before=cache_stats()
                    start=time.monotonic(); key('ret')
                    report=wait('PROFILE DONE')
                    elapsed=time.monotonic()-start
                    fields={}
                    for prefix in ('PROFILE rows','OPEN ticks','FRAME ticks'):
                        line=next(x for x in report.splitlines() if x.startswith(prefix))
                        namespace=prefix.split()[0].lower()
                        fields.update({namespace+'_'+k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
                    fields['host_seconds']=round(elapsed,4)
                    after=cache_stats()
                    fields.update({'cache_'+k:after[k]-v for k,v in before.items()})
                    results.append(fields)
                    print(json.dumps(fields),flush=True)
                qmp('quit'); proc.wait(timeout=5)
        finally:
            if proc.poll() is None: proc.terminate(); proc.wait(timeout=5)
    (out/(label+'.json')).write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label',default='current')
    parser.add_argument('--repeats',type=int,default=3)
    parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--reuse-binary',action='store_true')
    args=parser.parse_args()
    if not re.fullmatch(r'[a-zA-Z0-9_-]+',args.label) or args.repeats<1: parser.error('invalid label/repeats')
    run(args.label,args.repeats,args.build_only,args.reuse_binary)
