#!/usr/bin/env python3
"""Migrate a stopped legacy development disk, keeping an untouched backup."""
import argparse
import fcntl
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = ['shell', 'hello', 'cat', 'ls', 'echo', 'mkdir', 'rm', 'rmdir', 'meinkampf']


def quote(path):
    text = str(path)
    if '\n' in text or '\r' in text:
        raise ValueError('Newlines in paths are unsupported by debugfs')
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def migrate(image):
    tree = ROOT / 'ports/tinycc/root'
    if not (tree / 'usr/bin/kirkc').is_file():
        raise SystemExit('Run python3 ports/tinycc/build.py first')
    backup = image.with_name(image.name + '.before-layout.bak')
    if backup.exists():
        raise SystemExit(f'Backup already exists: {backup}; refusing to overwrite it')
    subprocess.run(['make', '-C', str(ROOT / 'user')], check=True)
    with image.open('r+b') as original:
        # QEMU uses POSIX byte-range locks; lock the entire disk before copying.
        try:
            fcntl.lockf(original, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Close QEMU before migrating this disk')
        subprocess.run(['e2fsck', '-fn', str(image)], check=True)
        with tempfile.TemporaryDirectory(prefix='zenos-layout-', dir=image.parent) as name:
            work = Path(name) / 'disk.img'
            shutil.copy2(image, work)

            def debug(command):
                result = subprocess.run(['debugfs', '-w', '-R', command, str(work)],
                                        text=True, capture_output=True, check=True)
                errors = result.stderr.splitlines()[1:]  # First line is the version.
                if errors:
                    raise RuntimeError(command + ': ' + '\n'.join(errors))
                return result.stdout

            def entries(directory):
                result = {}
                for line in debug('ls -p ' + quote(directory)).splitlines():
                    if not line:
                        continue
                    parts = line.split('/')
                    if len(parts) != 8:
                        raise RuntimeError('Unrecognized debugfs directory entry: ' + line)
                    if parts[1] != '0' and parts[5] not in ('.', '..'):
                        result[parts[5]] = int(parts[2], 8)
                return result

            def exists(path):
                return path.name in entries(path.parent)

            def mkdir(path):
                if path == Path('/'):
                    return
                mkdir(path.parent)
                found = entries(path.parent).get(path.name)
                if found is None:
                    debug('mkdir ' + quote(path))
                elif not stat.S_ISDIR(found):
                    raise RuntimeError(f'Expected a directory at {path}')

            def move(source, destination):
                if exists(destination):
                    raise RuntimeError(f'Destination already exists: {destination}')
                mode = entries(source.parent)[source.name]
                if stat.S_ISDIR(mode):
                    mkdir(destination)
                    for child in entries(source):
                        move(source / child, destination / child)
                    debug('rmdir ' + quote(source))
                else:
                    # Neither debugfs operation changes inode link counts.
                    debug('ln ' + quote(source) + ' ' + quote(destination))
                    debug('unlink ' + quote(source))

            for directory in ['/bin', '/usr/bin', '/home/user', '/tmp', '/etc']:
                mkdir(Path(directory))
            for old, new in [('/include', '/usr/include'), ('/lib', '/usr/lib')]:
                if exists(Path(old)):
                    move(Path(old), Path(new))
            for program in PROGRAMS:
                old = Path('/') / program
                if exists(old):
                    move(old, Path('/bin') / program)
            if exists(Path('/kirkc')):
                move(Path('/kirkc'), Path('/usr/bin/kirkc'))
            for example in (ROOT / 'ports/tinycc/examples').glob('*.c'):
                old = Path('/') / example.name
                if exists(old):
                    move(old, Path('/home/user') / example.name)

            def install(source, destination, replace=True):
                if exists(destination):
                    if not replace:
                        return
                    if not stat.S_ISREG(entries(destination.parent)[destination.name]):
                        raise RuntimeError(f'Expected a regular file at {destination}')
                    debug('rm ' + quote(destination))
                debug('write ' + quote(source) + ' ' + quote(destination))

            for program in PROGRAMS:
                install(ROOT / 'user' / program, Path('/bin') / program)
            for source in sorted(tree.rglob('*'), key=lambda p: (len(p.parts), str(p))):
                destination = Path('/') / source.relative_to(tree)
                if source.is_dir():
                    mkdir(destination)
                else:
                    install(source, destination, replace=not destination.is_relative_to('/home'))
            subprocess.run(['e2fsck', '-fn', str(work)], check=True)
            os.link(image, backup)
            os.replace(work, image)
    print(f'Migrated {image}\nOriginal disk preserved at {backup}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', nargs='?', type=Path, default=ROOT / 'disk/zenos-dev.img')
    migrate(parser.parse_args().image.resolve())
