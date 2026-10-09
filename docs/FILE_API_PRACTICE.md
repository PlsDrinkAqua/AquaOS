# File API practice on ZenOS

Work through these exercises in order. Each has a concrete pass condition; write
the programs yourself using the API notes below. The examples teach the building
blocks without providing complete exercise solutions.

## Where your code runs

Write ordinary user programs in C. Include `<unistd.h>`, `<stdio.h>`, and
`<errno.h>`. The functions in `userlibc` invoke your kernel's system calls, which
reach its file descriptor layer and ext2 driver. Kernel-internal `ext2_*`
functions are not the API to call from these programs.

Inside your existing ZenOS development environment:

```sh
cd /home/user
mkdir practice
cd practice
meinkampf create.c
tcc create.c -o create
./create
```

Create the directory once. In the editor, `i` enters Insert mode; Escape followed
by `:wq` and Enter saves and exits. Compile again after each edit. Use a different
output name from the source file. See [the editor guide](EDITOR.md) for details.
These commands run inside ZenOS; the workbook itself lives in the host repository.
The compiler setup is documented in [the TinyCC guide](../ports/tinycc/README.md).

## Your practice list

| Step | Program to write | APIs | Pass condition |
| --- | --- | --- | --- |
| 1 | `create`: create `note.txt` containing `Hello, files!\n` | `open`, `write`, `close` | `cat note.txt` shows one line; running twice still leaves one line. |
| 2 | `show`: print a file named by its first argument | `open`, `read`, `write`, `close` | Prints empty, short, and multi-buffer files correctly; a missing file reports an error. |
| 3 | `append`: add `Again!\n` to `note.txt` | `O_APPEND` | Each run adds exactly one line without removing previous text. |
| 4 | `count`: count bytes and newline characters | `read` | After step 1 and two appends, reports 27 bytes and 3 newlines. An empty file gives 0 and 0. |
| 5 | `patch`: modify a file at a byte offset | `lseek`, `O_RDWR` | Starting with `abcdef`, overwrite offset 2 with `XY`; result is `abXYef`, still 6 bytes. |
| 6 | `copy`: copy a source to a new destination | Read loop, write loop, `O_EXCL` | Exact contents for empty and multi-buffer files; an existing destination is refused and preserved. |
| 7 | `list`: print names in a directory | `open`, ZenOS `readdir`, `close` | Lists your practice files; passing an ordinary file reports an error. |
| 8 | `cleanup`: create and remove a scratch directory and file | `mkdir`, `unlink`, `rmdir` | Removing a nonempty directory fails; removing its file first lets directory removal succeed. |
| 9 | Final test: `filetool` | All previous skills | Meets the final test below. |

For every program: check arguments, report failures, close every successfully
opened descriptor on success and error paths, return 0 on success and nonzero
on failure. Keep your data files inside the practice directory.

## 1. Open a file and understand its descriptor

```c
int fd = open("example.txt", O_WRONLY | O_CREAT | O_TRUNC);
if (fd < 0) {
    printf("open failed: errno=%d\n", errno);
    return 1;
}
```

`fd` is a small integer handle owned by your process. It identifies an open file,
its access mode, and its current byte position. A relative path starts at your
current directory; `/home/user/practice/example.txt` is an absolute path.

| Flag | Meaning |
| --- | --- |
| `O_RDONLY` | Read only; this constant is zero. |
| `O_WRONLY` | Write only. |
| `O_RDWR` | Read and write. |
| `O_CREAT` | Create the file if absent; its parent directory must already exist. |
| `O_TRUNC` | Empty an existing file when opened for writing. |
| `O_APPEND` | Put every write at the current end of the file. |
| `O_EXCL` with `O_CREAT` | Fail if the destination already exists. |

Choose exactly one access mode, then combine additional flags using bitwise OR
(`|`). Opening an existing file for writing alone does not empty it.
ZenOS currently ignores the optional permission argument to `open`, so these
examples use two arguments. Your constants are available through `<unistd.h>`;
`<fcntl.h>` also includes them.

Standard descriptors are 0 for input, 1 for output, and 2 for errors. A valid
descriptor can be zero: test `fd < 0`, not `!fd`.

## 2. Write bytes, including partial writes

```c
const char text[] = "Example\n";
ssize_t n = write(fd, text, sizeof(text) - 1);
```

`write(fd, pointer, count)` returns the number of bytes written, or -1 on error.
The count is in bytes. `sizeof(text) - 1` excludes the terminating C string NUL;
it works here because `text` is an array. For a `char *`, use `strlen` from
`<string.h>` to find a string's length.

A write may accept fewer bytes than requested. This reusable helper finishes the
remaining bytes, and rejects a zero-byte write to avoid an infinite loop:

```c
static int write_all(int fd, const char *data, size_t length) {
    size_t done = 0;
    while (done < length) {
        ssize_t n = write(fd, data + done, length - done);
        if (n < 0) return -1;
        if (n == 0) { errno = EIO; return -1; }
        done += (size_t)n;
    }
    return 0;
}
```

Use `write_all(fd, text, sizeof(text) - 1)` for files and
`write_all(1, buffer, count)` for screen output. Check its return value.

`close(fd)` releases the handle and returns 0 on success, -1 on error. Check it
too, and do not use the descriptor afterward. If an earlier call failed, print
or save its `errno` before cleanup can change it.

## 3. Read until end of file

```c
char buffer[128];
ssize_t n = read(fd, buffer, sizeof(buffer));
```

For a positive requested count, `read` returns:

- A positive number: that many bytes are now in the buffer.
- Zero: end of file.
- -1: an error; inspect `errno`.

Read repeatedly until zero. A short positive read is still data; it does not
replace the next read that detects EOF. Both reads and writes advance the file
position by the number of bytes transferred.

`read` does not add a NUL terminator. To display arbitrary file contents, pass
exactly the returned byte count to `write_all`. Do not pass the buffer to `%s`.
For a deliberately text-only snippet, reserve a byte and terminate it yourself:

```c
ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
if (n >= 0) {
    buffer[n] = '\0';
    printf("%s", buffer);
}
```

For step 4, add each positive read count to the byte total, and examine only
`buffer[0]` through `buffer[n - 1]` for `'\n'`. Count newline characters, including
zero for a nonempty file with no newline. This makes the test unambiguous.
For step 6, finish writing each chunk before reading the next one. Never assume
the whole file fits in your buffer.

## 4. Move the file position

```c
off_t pos = lseek(fd, 2, SEEK_SET);
if (pos < 0) {
    printf("seek failed: errno=%d\n", errno);
}
```

`lseek` returns the new absolute byte position, or -1 on error.

| Call | Effect |
| --- | --- |
| `lseek(fd, 0, SEEK_SET)` | Return to the start. |
| `lseek(fd, 2, SEEK_SET)` | Move to offset 2, the third byte. |
| `lseek(fd, 0, SEEK_CUR)` | Get the current position. |
| `lseek(fd, 3, SEEK_CUR)` | Advance three bytes. |
| `lseek(fd, 0, SEEK_END)` | Move to EOF and obtain the file length. |
| `lseek(fd, -2, SEEK_END)` | Move two bytes before EOF, if the file is long enough. |

Seeking changes position, not contents. A subsequent write overwrites bytes at
that position; it does not insert bytes and shift the rest. For step 5, open with
`O_RDWR`, without truncation or append. Append would send the write to EOF.
After measuring a file with `SEEK_END`, seek back before trying to read it.

## 5. Directories and deletion

ZenOS has its own directory API; it does not use the POSIX `DIR *` interface:

```c
int fd = open(".", O_RDONLY);
/* Check fd before continuing. */
char name[256];
int result;
while ((result = readdir(fd, name, sizeof(name))) > 0) {
    printf("%s\n", name);
}
/* result == 0 means end; result < 0 means error. Close fd either way. */
```

Each successful call returns 1 and writes one NUL-terminated name. Names may
include `.` and `..`; you can skip them using `strcmp` from `<string.h>`.
Names are relative to the directory you opened, not necessarily your process's
current directory. Listing order is unspecified.

`mkdir("scratch")` creates a directory. ZenOS takes one argument here.
`unlink("scratch/data.txt")` removes a file's name.
`rmdir("scratch")` removes an empty directory. Each returns 0 on success or -1
on error. Close directory descriptors and leave a directory before removing it.
`chdir(path)` changes the working directory and follows the same return rule.
`getcwd(buffer, sizeof(buffer))` returns the buffer on success or NULL on error.

Optional later exercise: `rename` is declared in `<stdio.h>`. Your kernel
currently supports regular-file renames within one directory, with additional
directory-entry space limits; do not assume general directory moves work.

## 6. Understand errors

User-library wrappers return -1 and set a positive `errno`. They translate the
kernel's internal negative error codes for you. Only inspect `errno` after a
call reports failure; a successful call need not clear an old error.

| Error | Useful case to test |
| --- | --- |
| `ENOENT` | Read a missing file or create beneath a missing parent. |
| `EEXIST` | Create an existing file using `O_CREAT | O_EXCL`. |
| `EBADF` | Read using a write-only descriptor. |
| `EISDIR` | Try to read a directory as ordinary file bytes. |
| `ENOTDIR` | Call `readdir` on an ordinary file. |
| `ENOTEMPTY` | Remove a directory that still contains your file. |

Print errors with `printf("operation failed: errno=%d\n", errno)` or
`fprintf(stderr, ...)`. Your public stdio header does not expose `perror`.

## Final test: filetool

Implement these subcommands, using the descriptor APIs above:

```sh
./filetool write note.txt "alpha"
./filetool append note.txt "beta"
./filetool show note.txt
./filetool size note.txt
./filetool copy note.txt backup.txt
./filetool list .
./filetool delete note.txt
```

For this test, `write` replaces the file and `append` adds to it; both add one
newline after the supplied text. `show` adds nothing of its own. `size` prints
the byte length. `copy` requires a destination that does not already exist.
Use `argc` to check argument counts and `strcmp(argv[1], "write") == 0` to
identify a subcommand. In the first command, `argv[2]` is the path and `argv[3]`
is the text. Print usage and return nonzero for invalid commands or arguments.

Pass these checks:

1. After the first two commands, `show` prints `alpha` and `beta` on separate
   lines, and `size` prints 11.
2. `backup.txt` contains identical bytes, and remains readable after deleting
   `note.txt`.
3. Repeating the copy to an existing backup fails without changing that backup.
4. Showing a missing file and deleting a nonempty directory both fail clearly.
5. Copy an empty file and a file larger than your buffer (at least 4097 bytes);
   both match byte for byte. Generate the large input by repeatedly writing a
   known pattern in a small C program.
6. Copy data containing a NUL byte, such as `{'A', '\0', 'B', '\n'}`. Verify with
   a small C comparison loop; terminal display cannot establish byte equality.
7. Every opened descriptor is closed on all exit paths, including after errors.

Once this works, repeat `show` and `copy` using `fopen`, `fread`, `fwrite`, and
`fclose`. These use `FILE *` streams on top of the same kernel operations.
`fopen` returns NULL on failure; modes `"rb"`, `"wb"`, and `"ab"` mean read,
replace, and append. `fread`/`fwrite` return item counts: use an item size of 1
to work in bytes. After a short read, use `ferror` to distinguish an error from
EOF, and check `fclose` for failure. Finish the descriptor exercises first.

## Source references

- [Public descriptor API](../userlibc/include/unistd.h)
- [Error translation](../userlibc/include/zenos/sysret.h)
- [Existing cat implementation](../user/cat.c), useful after attempting step 2
- [Existing directory listing](../user/ls.c), useful after attempting step 7
- [Kernel filesystem implementation](../kernel/kernel/FILESYSTEM/ext2_api.c)

This workbook was checked against the repository's current API declarations and
implementation. The exercises are assignments, not programs already run or graded.
