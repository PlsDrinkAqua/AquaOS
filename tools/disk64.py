#!/usr/bin/env python3
"""Create a new x86-64 development disk without overwriting an existing disk."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/x86_64'

def create(output):
    output = output.resolve()
    if output.exists():
        raise SystemExit(f'Refusing to overwrite {output}; choose a new path')
    tree = OUT / 'root'
    if not (tree/'usr/bin/tcc').is_file():
        raise SystemExit('Run python3 ports/tinycc/build.py --arch x86_64 first')
    output.parent.mkdir(parents=True, exist_ok=True)
    def quote(path):
        value = str(path)
        if '\n' in value or '\r' in value:
            raise ValueError('Newlines in disk installation paths are unsupported')
        return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
    with tempfile.TemporaryDirectory(prefix='zenos64-', dir=output.parent) as tmp:
        image = Path(tmp)/'disk.img'
        with image.open('wb') as stream:
            stream.truncate(100 * 1024 * 1024)
        subprocess.run(['mkfs.ext2', '-q', '-F', '-b', '4096', str(image)], check=True)
        commands = ['mkdir /bin', 'mkdir /tmp', 'mkdir /etc']
        for program in sorted((OUT/'user').iterdir()):
            if program.is_file():
                commands.append(f'write {quote(program)} {quote("/bin/" + program.name)}')
        for entry in sorted(tree.rglob('*'), key=lambda p:(len(p.parts),str(p))):
            target = '/' + entry.relative_to(tree).as_posix()
            if entry.is_file() and entry.stat().st_size > (12 + 1024) * 4096:
                raise RuntimeError(f'File exceeds filesystem capacity: {entry}')
            commands.append(f'mkdir {quote(target)}' if entry.is_dir()
                            else f'write {quote(entry)} {quote(target)}')
        batch = Path(tmp)/'install.txt'
        batch.write_text('\n'.join(commands)+'\n')
        result = subprocess.run(['debugfs', '-w', '-f', str(batch), str(image)],
                                text=True, capture_output=True, check=True)
        if 'while ' in result.stderr or 'not found' in result.stderr.lower():
            raise RuntimeError(result.stderr)
        subprocess.run(['e2fsck', '-fn', str(image)], check=True)
        os.link(image, output)
    print('Created', output)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', nargs='?', type=Path, default=ROOT/'disk/zenos64-dev.img')
    create(parser.parse_args().output)
