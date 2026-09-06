#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zenos/readline.h>
#include <zenos/task.h>
#include <zenos/terminal.h>

#define LINE_MAX 128

static void write_str(const char *s) {
    const char *p = s;

    while (*p) {
        p++;
    }

    write(1, s, (unsigned)(p - s));
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ') s++;
    return s;
}

static void cat_file(const char *path) {
    char buf[128];
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        printf("cat: cannot open %s\n", path);
        return;
    }

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            printf("cat: cannot read %s\n", path);
            break;
        }
        if (n == 0) break;
        if (write(1, buf, (size_t)n) != n) break;
    }
    close(fd);
}

static void list_dir(const char *path) {
    char name[256];
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        printf("ls: cannot open %s\n", path);
        return;
    }

    for (;;) {
        int result = readdir(fd, name, sizeof(name));
        if (result < 0) {
            printf("ls: cannot read %s\n", path);
            break;
        }
        if (result == 0) break;
        puts(name);
    }
    close(fd);
}

static void run_command(const char *line) {
    if (line[0] == '\0') {
        return;
    }

    if (strcmp(line, "help") == 0) {
        puts("Commands: help, echo, about, clear, hello, memtest, yieldtest, sleeptest, faulttest, tasktest, fdtest, cat, ls, taskinfo");
        return;
    }

    if (strcmp(line, "clear") == 0) {
        zenos_terminal_clear();
        return;
    }

    if (strcmp(line, "about") == 0) {
        puts("AquaOS userland shell");
        return;
    }

    if (strcmp(line, "taskinfo") == 0) {
        zenos_taskinfo();
        return;
    }

    if (strcmp(line, "hello") == 0) {
        int pid = spawn("/hello");
        if (pid < 0) {
            printf("spawn failed: /hello\n");
        } else if (wait(pid) < 0) {
            printf("wait failed: %d\n", pid);
        }
        return;
    }

    if (strcmp(line, "memtest") == 0) {
        int pid = spawn("/memtest");
        if (pid < 0) {
            printf("spawn failed: /memtest\n");
        } else if (wait(pid) < 0) {
            printf("wait failed: %d\n", pid);
        }
        return;
    }

    if (strcmp(line, "yieldtest") == 0) {
        int pid = spawn("/yieldtest");
        if (pid < 0) {
            printf("spawn failed: /yieldtest\n");
        } else if (wait(pid) < 0) {
            printf("wait failed: %d\n", pid);
        }
        return;
    }

    if (strcmp(line, "sleeptest") == 0) {
        int pid = spawn("/sleeptest");
        if (pid < 0) {
            printf("spawn failed: /sleeptest\n");
        } else if (wait(pid) < 0) {
            printf("wait failed: %d\n", pid);
        }
        return;
    }

    if (strcmp(line, "faulttest") == 0) {
        int pid = spawn("/faulttest");
        if (pid < 0) {
            printf("spawn failed: /faulttest\n");
        } else {
            int status = wait(pid);
            printf("faulttest exited with status %d (expected 142)\n", status);
        }
        return;
    }

    if (strcmp(line, "tasktest") == 0) {
        int pid = spawn("/tasktest");
        if (pid < 0) {
            printf("spawn failed: /tasktest\n");
        } else {
            printf("tasktest exited with status %d\n", wait(pid));
        }
        return;
    }

    if (strcmp(line, "fdtest") == 0) {
        int pid = spawn("/fdtest");
        if (pid < 0) {
            printf("spawn failed: /fdtest\n");
        } else {
            printf("fdtest exited with status %d\n", wait(pid));
        }
        return;
    }

    if (strncmp(line, "cat ", 4) == 0) {
        const char *path = skip_spaces(line + 4);
        if (*path == '\0') puts("usage: cat <path>");
        else cat_file(path);
        return;
    }

    if (strcmp(line, "ls") == 0) {
        list_dir("/");
        return;
    }

    if (strncmp(line, "ls ", 3) == 0) {
        const char *path = skip_spaces(line + 3);
        if (*path == '\0') list_dir("/");
        else list_dir(path);
        return;
    }

    if (strncmp(line, "echo ", 5) == 0) {
        puts(line + 5);
        return;
    }

    printf("unknown command: %s\n", line);
}

void _start(void) {
    char line[LINE_MAX];

    printf("ZenOS shell at %x\n", 0x08048000);

    for (;;) {
        write_str("ZenOS> ");
        zenos_readline(line, sizeof(line));
        run_command(line);
    }

}
