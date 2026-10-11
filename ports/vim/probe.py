#!/usr/bin/env python3
"""Audit a pinned tiny Vim against ZenOS; does not install or patch Vim."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/x86_64/vim-probe'
REV = 'v9.1.0000'
SHA = '8d2a74e358be9cf260174e15e2e29dba4773f2670718b5bf728db6f54b90e25d'
CC = os.environ.get('ZENOS64_CC', 'gcc')
BUILD.mkdir(parents=True, exist_ok=True)
archive = BUILD / 'source.tar.gz'
if not archive.exists():
    with urllib.request.urlopen(f'https://codeload.github.com/vim/vim/tar.gz/refs/tags/{REV}', timeout=60) as response:
        archive.write_bytes(response.read())
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA:
    raise SystemExit('Vim archive checksum mismatch')
source = BUILD / 'vim-9.1.0000/src'
if not source.exists():
    with tarfile.open(archive) as tar:
        tar.extractall(BUILD, filter='data')
libc = ROOT / 'build/x86_64/libc.a'
crt = ROOT / 'build/x86_64/obj/userlibc/arch/x86_64/crt0.o'
if not libc.exists() or not crt.exists():
    raise SystemExit('Run sh build.sh first')
compiler_headers = subprocess.check_output([CC, '-print-file-name=include'], text=True).strip()
flags = ['-m64', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
         '-fno-pie', '-mno-red-zone', '-nostdinc', '-isystem', compiler_headers,
         '-I' + str(ROOT / 'userlibc/include'), '-D__zenos__=1', '-U__linux__', '-U__unix__']
link = ['-no-pie', '-nostdlib', '-T', str(ROOT / 'user/user.ld'), str(crt)]
wrapper = BUILD / 'cc'
# Reject explicit host libraries: configure must not discover Linux libraries.
wrapper.write_text('#!/bin/bash\nfor arg in "$@"; do\n'
                   ' case "$arg" in -l*|-L*) exit 1;; esac\ndone\n'
                   'for arg in "$@"; do\n case "$arg" in -c|-E|-S) exec '
                   + shlex.join([CC, *flags]) + ' "$@";; esac\ndone\nexec '
                   + shlex.join([CC, *flags, *link]) + ' "$@" '
                   + shlex.join([str(libc), '-lgcc']) + '\n')
wrapper.chmod(0o755)
command = ['sh', 'configure', '--build=x86_64-pc-linux-gnu', '--host=x86_64-unknown-none',
           '--with-features=tiny', '--disable-gui', '--without-x', '--disable-nls',
           '--disable-channel', '--disable-netbeans',
           '--disable-selinux', '--disable-smack', '--disable-xsmp',
           'CC=' + str(wrapper), 'vim_cv_toupper_broken=no',
           'vim_cv_uname_output=ZenOS', 'vim_cv_uname_r_output=0', 'vim_cv_getcwd_broken=no']
(BUILD / 'configure-command.txt').write_text(shlex.join(command) + '\n')
with (BUILD / 'configure.log').open('w') as log:
    configured = subprocess.run(command, cwd=source, stdout=log, stderr=subprocess.STDOUT)

# Independent header and symbol probes continue after configure's first blocker.
headers = ['sys/types.h', 'sys/stat.h', 'time.h', 'signal.h', 'dirent.h',
           'termios.h', 'sys/ioctl.h', 'sys/select.h', 'poll.h', 'sys/time.h',
           'sys/wait.h', 'termcap.h', 'pwd.h', 'utime.h', 'wchar.h', 'wctype.h']
symbols = ['stat', 'fstat', 'lstat', 'access', 'chmod', 'fchmod', 'utime',
           'opendir', 'closedir', 'tcgetattr', 'tcsetattr', 'ioctl', 'select',
           'poll', 'gettimeofday', 'time', 'signal', 'sigaction', 'kill',
           'waitpid', 'isatty', 'getpid', 'fsync', 'fcntl', 'tgetent',
           'tgetstr', 'tgetnum', 'tgetflag', 'tgoto', 'tputs',
           'read', 'write', 'open', 'close', 'rename', 'fork', 'getenv', 'malloc']
results = {'revision': REV, 'configure_exit': configured.returncode, 'headers': {}, 'symbols': {}}
probe = BUILD / 'probe.c'
for header in headers:
    probe.write_text(f'#include <{header}>\nint main(void) {{ return 0; }}\n')
    result = subprocess.run([CC, *flags, '-c', str(probe), '-o', str(BUILD / 'probe.o')], capture_output=True, text=True)
    results['headers'][header] = result.returncode == 0
    (BUILD / ('header-' + header.replace('/', '-') + '.log')).write_text(result.stderr)
for symbol in symbols:
    # Address-only test: proves symbol availability, not its signature/semantics.
    probe.write_text(f'extern void {symbol}(void);\nvoid (*volatile reference)(void) = {symbol};\nint main(void) {{ return reference == 0; }}\n')
    result = subprocess.run([CC, *flags, *link, str(probe), str(libc), '-lgcc', '-o', str(BUILD / 'probe.elf')], capture_output=True, text=True)
    results['symbols'][symbol] = result.returncode == 0
    (BUILD / ('symbol-' + symbol + '.log')).write_text(result.stderr)
probe.write_text('#include "vim.h"\n')
result = subprocess.run([CC, *flags, '-DFEAT_TINY', '-DUNIX', '-DVIM_SIZEOF_INT=4', '-I' + str(source), '-c', str(probe), '-o', str(BUILD / 'vim-header.o')], capture_output=True, text=True)
(BUILD / 'vim-header.log').write_text(result.stderr)
results['vim_header_exit'] = result.returncode
(BUILD / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
print('Logs:', BUILD)
print('This audit does not produce a Vim executable or test runtime behavior.')
