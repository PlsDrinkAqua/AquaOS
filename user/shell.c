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

static void run_command(const char *line) {
    if (line[0] == '\0') {
        return;
    }

    if (strcmp(line, "help") == 0) {
        puts("Commands: help, echo, about, clear, hello, memtest, yieldtest, sleeptest, taskinfo");
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

    if (strncmp(line, "echo ", 5) == 0) {
        puts(line + 5);
        return;
    }

    printf("unknown command: %s\n", line);
}

void _start(void) {
    char line[LINE_MAX];

    printf("AquaOS shell at %x\n", 0x08048000);

    for (;;) {
        write_str("AquaOS> ");
        zenos_readline(line, sizeof(line));
        run_command(line);
    }

}
