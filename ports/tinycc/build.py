#!/usr/bin/env python3
"""Build pinned TinyCC for ZenOS; output a disk-installable tree in root/."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tarfile
import urllib.request

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REV = 'd348a9a51d32cece842b7885d27a411436d7887b'
SHA = 'f94573de2b9061473d4f40cbf9c8a8ea9ac64d9c7a4abf75b008d1401fcf3f96'
CACHE, BUILD, DEST = [HERE / n for n in ('cache', 'build', 'root')]
CACHE.mkdir(exist_ok=True)
archive = CACHE / 'source.tar.gz'
if not archive.exists():
    with urllib.request.urlopen(f'https://codeload.github.com/TinyCC/tinycc/tar.gz/{REV}', timeout=60) as r:
        archive.write_bytes(r.read())
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA:
    raise SystemExit('TinyCC source archive checksum mismatch')
if BUILD.exists(): shutil.rmtree(BUILD)
BUILD.mkdir()
with tarfile.open(archive) as tar:
    tar.extractall(BUILD, filter='data')
source = BUILD / ('tinycc-' + REV)

def replace(file, old, new):
    p = source / file
    s = p.read_text()
    if s.count(old) != 1: raise RuntimeError(f'Patch mismatch: {file}: {old[:60]}')
    p.write_text(s.replace(old, new))

replace('tcc.h', '#include <time.h>', '')
replace('tcc.h', '# include <sys/time.h>', '')
replace('tcc.h', '#if defined _WIN32 == defined TCC_TARGET_PE', '#if !defined(__zenos__) && (defined _WIN32 == defined TCC_TARGET_PE)')
replace('tcc.h', '#include <stdlib.h>', '#include <stdlib.h>\n/* No environment ABI yet: configured compiler paths are authoritative. */\nstatic inline char *getenv(const char *name) { (void)name; return NULL; }')
replace('tcc.c', '    struct timeval tv;\n    gettimeofday(&tv, NULL);\n    return tv.tv_sec*1000 + (tv.tv_usec+500)/1000;', '    return 0; /* No compiler timing until ZenOS has a clock API. */')
p = source / 'tccpp.c'; s = p.read_text(); a = s.index('        time_t ti;'); b = s.index('        cstrval = buf;', a)
s = s[:a] + '        strcpy(buf, tok == TOK___DATE__ ? "Jan  1 1970" : "00:00:00");\n' + s[b:]; p.write_text(s)
p = source / 'tcctools.c'; s = p.read_text(); a = s.index('    char program[4096];', s.index('ST_FUNC void tcc_tool_cross')); b = s.index('\n}', a)
s = s[:a] + '    (void)s; (void)argv; (void)target;\n    tcc_error("ZenOS supports only the i386 target");' + s[b:]; p.write_text(s)
replace('libtcc.c', '            s->do_bench = 1;', '            tcc_error("compiler timing is not available on ZenOS");')
replace('libtcc.c', '    s->output_type = output_type;', '    if (output_type == TCC_OUTPUT_DLL || output_type == TCC_OUTPUT_MEMORY)\n        tcc_error("ZenOS supports compile-to-file only");\n    s->output_type = output_type;')
replace('libtcc.c', '    s->alacarte_link = 1;', '    s->static_link = 1;\n    s->alacarte_link = 1;')
replace('libtcc.c', '    tcc_define_symbol(s, "__STDC__", NULL);', '    tcc_define_symbol(s, "__zenos__", "1");\n    tcc_define_symbol(s, "__STDC__", NULL);')
replace('libtcc.c', '            tcc_add_crt(s, "crt1.o");\n        tcc_add_crt(s, "crti.o");', '            tcc_add_crt(s, "crt0.o");')
replace('tccelf.c', '        if (s1->output_type != TCC_OUTPUT_MEMORY)\n            tcc_add_crt(s1, "crtn.o");', '        tcc_add_library_err(s1, "gcc"); /* GCC-built ZenOS libc helpers. */')
replace('tccelf.c', '    f = fdopen(fd, "wb");', '    f = fdopen(fd, "wb");\n    if (!f) { close(fd); unlink(filename); tcc_error_noabort("cannot open output stream"); return -1; }')
replace('tccelf.c', '    fclose(f);\n\n    return 0;', '    {\n        int failed = ferror(f);\n        if (fclose(f) < 0) failed = 1;\n        if (failed) { unlink(filename); tcc_error_noabort("output write failed"); return -1; }\n    }\n\n    return 0;')
(source / 'config.h').write_text('''#define TCC_VERSION "0.9.27"
#define CONFIG_TCC_STATIC 1
#define TCC_TARGET_I386 1
#define CONFIG_TCCDIR "/lib/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "/include"
#define CONFIG_TCC_LIBPATHS "/lib"
#define CONFIG_TCC_CRTPREFIX "/lib"
''')
cc = os.environ.get('ZENOS_CC', 'i686-elf-gcc')
ar = os.environ.get('ZENOS_AR', 'i686-elf-ar')
def run(args): subprocess.run(list(map(str, args)), check=True, cwd=ROOT)
run(['make', '-C', 'userlibc'])
flags = ['-O2', '-g', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-D__zenos__=1', '-I'+str(ROOT/'userlibc/include')]
run([cc, *flags, '-c', source/'tcc.c', '-o', BUILD/'tcc.o'])
if DEST.exists(): shutil.rmtree(DEST)
(DEST/'lib/tcc').mkdir(parents=True)
run([cc, '-T', ROOT/'user/user.ld', '-o', DEST/'kirkc', ROOT/'userlibc/crt0.o', BUILD/'tcc.o', ROOT/'userlibc/libc.a', '-nostdlib', '-lgcc', '-Wl,-s'])
for name in ['libtcc1.c', 'alloca86.S', 'alloca86-bt.S']:
    run([cc, *flags, '-c', source/'lib'/name, '-o', BUILD/(Path(name).stem+'.o')])
run([ar, 'rcs', DEST/'lib/tcc/libtcc1.a', *[BUILD/(n+'.o') for n in ['libtcc1','alloca86','alloca86-bt']]])
shutil.copy2(ROOT/'userlibc/libc.a', DEST/'lib/libc.a')
shutil.copy2(ROOT/'userlibc/crt0.o', DEST/'lib/crt0.o')
libgcc = subprocess.check_output([cc, '-print-libgcc-file-name'], text=True).strip()
shutil.copy2(libgcc, DEST/'lib/libgcc.a')
shutil.copytree(ROOT/'userlibc/include', DEST/'include')
for name in ['stdarg.h', 'stdbool.h', 'float.h']:
    shutil.copy2(source/'include'/name, DEST/'include'/name)
shutil.copy2(source/'COPYING', DEST/'lib/tcc/COPYING')
for example in (HERE/'examples').glob('*.c'):
    shutil.copy2(example, DEST/example.name)
print('TinyCC installation tree:', DEST)
