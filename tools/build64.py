#!/usr/bin/env python3
"""Build the freestanding x86-64 kernel and userspace into build/x86_64."""
from pathlib import Path
import os
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/x86_64'
CC = shlex.split(os.environ.get('ZENOS64_CC', 'gcc'))
AR = shlex.split(os.environ.get('ZENOS64_AR', 'ar'))
COMPILER_HEADERS = subprocess.check_output([*CC, '-print-file-name=include'], text=True).strip()
FLAGS = ['-nostdinc', '-isystem', COMPILER_HEADERS, '-m64', '-O2', '-g', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
         '-fno-pie', '-fcf-protection=none', '-fno-asynchronous-unwind-tables', '-mno-red-zone', '-Wall', '-Wextra',
         '-Werror=pointer-to-int-cast', '-Werror=int-to-pointer-cast']
KFLAGS = ['-D__is_libk', '-D__is_kernel', '-mcmodel=kernel', '-mgeneral-regs-only', '-I'+str(ROOT/'kernel/include'),
          '-I'+str(ROOT/'libk/include'), '-I'+str(ROOT/'userlibc/include')]
UFLAGS = ['-I'+str(ROOT/'userlibc/include')]
PROGRAMS = ['shell', 'hello', 'cat', 'cp', 'mv', 'ls', 'echo', 'mkdir', 'rm', 'rmdir', 'kilo', 'ping']
TESTS = {'fscheck': 'fs_delete', 'memcheck': 'memory_stress', 'libccheck': 'libc_core',
         'stdiocheck': 'stdio_core', 'intcheck': 'interrupt', 'abi64check': 'abi64'}

def run(args):
    subprocess.run([str(a) for a in args], check=True, cwd=ROOT)

def compile_file(source, extra):
    source = Path(source)
    target = OUT / 'obj' / source.with_suffix('.o')
    target.parent.mkdir(parents=True, exist_ok=True)
    run([*CC, *FLAGS, *extra, '-c', ROOT/source, '-o', target])
    return target

def build():
    OUT.mkdir(parents=True, exist_ok=True)
    # Remove obsolete editor artifacts so disk generation cannot reinstall it.
    for obsolete in [OUT/'user/meinkampf', OUT/'obj/user/meinkampf.o']:
        obsolete.unlink(missing_ok=True)
    sources = [p.relative_to(ROOT) for p in (ROOT/'kernel/kernel').rglob('*.c')]
    sources += [p.relative_to(ROOT) for p in (ROOT/'kernel/arch/x86_64').glob('*.c')]
    sources += [p.relative_to(ROOT) for p in (ROOT/'libk').rglob('*.c')]
    objects = [compile_file(p, KFLAGS) for p in sources]
    objects += [compile_file(Path('kernel/arch/x86_64')/p, KFLAGS) for p in ['boot.S','interrupts.S']]
    run([*CC, *FLAGS, '-mcmodel=kernel', '-nostdlib', '-no-pie',
         '-Wl,--build-id=none,-z,max-page-size=0x1000', '-T', ROOT/'kernel/arch/x86_64/linker.ld',
         '-o', OUT/'myos.kernel', *objects, '-lgcc'])
    run(['grub-file', '--is-x86-multiboot', OUT/'myos.kernel'])
    libc = [compile_file(p.relative_to(ROOT), UFLAGS) for p in (ROOT/'userlibc').rglob('*.c')]
    libc.append(compile_file('userlibc/arch/x86_64/setjmp.S', UFLAGS))
    run([*AR, 'rcs', OUT/'libc.a', *libc])
    crt = compile_file('userlibc/arch/x86_64/crt0.S', UFLAGS)
    (OUT/'user').mkdir(exist_ok=True)
    for name in PROGRAMS + list(TESTS):
        source = Path('tests')/(TESTS[name]+'.c') if name in TESTS else Path('user')/(name+'.c')
        obj = compile_file(source, UFLAGS)
        run([*CC, *FLAGS, '-nostdlib', '-no-pie', '-Wl,--build-id=none',
             '-T', ROOT/'user/user.ld', '-o', OUT/'user'/name, crt, obj, OUT/'libc.a', '-lgcc'])
    print('Built x86-64 kernel and userspace:', OUT)

if __name__ == '__main__':
    build()
