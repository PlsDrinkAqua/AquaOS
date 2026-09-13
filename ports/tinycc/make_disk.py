#!/usr/bin/env python3
"""Create a new development disk containing the staged native compiler."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', nargs='?', type=Path, default=ROOT/'disk/zenos-dev.img')
args = parser.parse_args()
output = args.output.resolve()
if output.exists(): raise SystemExit(f'Refusing to overwrite {output}; choose a new output path')
tree = ROOT/'ports/tinycc/root'
if not (tree/'kirkc').is_file(): raise SystemExit('Run python3 ports/tinycc/build.py first')
subprocess.run(['make', '-C', str(ROOT/'user')], check=True)
output.parent.mkdir(parents=True, exist_ok=True)
def quote(path): return '"' + str(path).replace('\\', '\\\\').replace('"', '\\"') + '"'
with tempfile.TemporaryDirectory(prefix='zenos-dev-', dir=output.parent) as name:
    work = Path(name)
    image = work/'disk.img'
    with image.open('wb') as f: f.truncate(100*1024*1024)
    subprocess.run(['mkfs.ext2', '-q', '-F', '-b', '4096', str(image)], check=True)
    commands = []
    for program in ['shell','hello','cat','ls','echo','mkdir','rm','rmdir','meinkampf']:
        commands.append(f'write {quote(ROOT/"user"/program)} /{program}')
    for entry in sorted(tree.rglob('*'), key=lambda p: (len(p.parts), str(p))):
        target = '/' + entry.relative_to(tree).as_posix()
        if entry.is_file() and entry.stat().st_size > (12+4096//4)*4096:
            raise SystemExit(f'File exceeds ZenOS single-indirect capacity: {entry}')
        commands.append(f'mkdir {quote(target)}' if entry.is_dir() else f'write {quote(entry)} {quote(target)}')
    batch = work/'install.txt'; batch.write_text('\n'.join(commands)+'\n')
    result = subprocess.run(['debugfs', '-w', '-f', str(batch), str(image)], text=True, capture_output=True, check=True)
    if 'while ' in result.stderr or 'not found' in result.stderr.lower():
        raise RuntimeError(result.stderr)
    subprocess.run(['e2fsck', '-fn', str(image)], check=True)
    # Atomic creation: even a concurrent invocation cannot overwrite a disk.
    os.link(image, output)
print('Created', output)
print('Boot: qemu-system-i386 -m 128M -cdrom myos.iso -drive file=' + str(output) + ',format=raw,if=ide')
