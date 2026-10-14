# Copying and moving files

AquaOS installs `cp` and `mv` in `/bin`.

```sh
cp hello.c backup.c
mv backup.c example.c
mkdir sources
cp hello.c example.c sources
mv example.c sources
```

Each command accepts one or more source files followed by a destination. A
destination directory receives each source's basename. Multiple sources require
an existing destination directory. Relative paths, quoted filenames, and
absolute paths work. Existing destination files are replaced without prompting.
These initial commands support regular files; options and recursive directory
copying/moving are not implemented. Any failed operand makes the exit status
nonzero, while later operands are still attempted.

`cp` reads in blocks and handles short writes. It creates an exclusive numbered
temporary file beside the destination, then closes and renames it into place.
Ordinary read/write/close errors leave the existing destination unchanged.
Existing temporary names are skipped, up to 32 attempts; cleanup is attempted
after a failure. Copying a path onto itself, including lexical `.`/`..` aliases,
is rejected. Distinct hard-link names are copied into separate inodes rather
than detected as the same file, preserving the source's contents.

`mv` first calls `rename`. The current kernel supports regular-file renames in
one directory. If a cross-directory move returns EXDEV, or a longer destination
name will not fit the source directory entry, `mv` stages a copy at the new
location and removes the source only after the destination is complete. If
source removal fails, both files remain and the command reports that condition.
This fallback is not atomic and creates a new inode; it does not preserve hard
links, ownership, permissions, timestamps, or sparse allocation. Ordinary copies
also create a new inode with default file permissions. Same-directory renames
preserve the source inode. Filesystem mutations are not journaled.

The temporary suffix needs space within AquaOS's 255-byte path limit. A very long
destination path can therefore fail even if a direct open would accept it.

Verification:

```sh
python3 tests/file_ops_host.py
sh build.sh
sh iso.sh
python3 tests/shell_smoke.py --block-size 4096 --file-ops-only
```

The host checks use sanitizers and injected read, write, rename and unlink
failures. QEMU checks exercise actual commands, replacement, same-path errors,
multiple sources, cross-directory moves, and larger files. Offline debugfs and
e2fsck verify contents, removed source names, and filesystem consistency.
