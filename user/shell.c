#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zenos/readline.h>
#include <zenos/task.h>
#include <zenos/terminal.h>

#define LINE_MAX 128
#define PIPELINE_MAX 4

typedef struct {
    char command[LINE_MAX];
    char input[LINE_MAX];
    char output[LINE_MAX];
    int append;
} shell_stage_t;

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

static void copy_stream(int input) {
    char buf[128];
    for (;;) {
        ssize_t n = read(input, buf, sizeof(buf));
        if (n <= 0) return;
        size_t done = 0;
        while (done < (size_t)n) {
            ssize_t written = write(1, buf + done, (size_t)n - done);
            if (written <= 0) return;
            done += (size_t)written;
        }
    }
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

static int has_shell_operator(const char *line) {
    while (*line) {
        if (*line == '|' || *line == '<' || *line == '>') return 1;
        line++;
    }
    return 0;
}

static void copy_trimmed(char *dest, size_t size, const char *begin, const char *end) {
    size_t len;
    while (begin < end && *begin == ' ') begin++;
    while (end > begin && end[-1] == ' ') end--;
    len = (size_t)(end - begin);
    if (len >= size) len = size - 1;
    for (size_t i = 0; i < len; i++) dest[i] = begin[i];
    dest[len] = '\0';
}

static int parse_stage(const char *begin, const char *end, shell_stage_t *stage) {
    const char *p = begin;
    const char *command_end = end;
    stage->command[0] = stage->input[0] = stage->output[0] = '\0';
    stage->append = 0;
    while (p < end) {
        if (*p == '<' || *p == '>') {
            char op = *p++;
            const char *name_begin, *name_end;
            if (command_end == end) command_end = p - 1;
            if (op == '>' && p < end && *p == '>') { stage->append = 1; p++; }
            while (p < end && *p == ' ') p++;
            name_begin = p;
            while (p < end && *p != '<' && *p != '>') p++;
            name_end = p;
            while (name_end > name_begin && name_end[-1] == ' ') name_end--;
            if (name_begin == name_end) return -1;
            if (op == '<') copy_trimmed(stage->input, sizeof(stage->input), name_begin, name_end);
            else copy_trimmed(stage->output, sizeof(stage->output), name_begin, name_end);
        } else p++;
    }
    copy_trimmed(stage->command, sizeof(stage->command), begin, command_end);
    return stage->command[0] ? 0 : -1;
}

static int parse_pipeline(const char *line, shell_stage_t stages[], int *count) {
    const char *start = line, *p = line;
    int n = 0;
    for (;;) {
        if (*p == '|' || *p == '\0') {
            if (n == PIPELINE_MAX || parse_stage(start, p, &stages[n]) < 0) return -1;
            n++;
            if (*p == '\0') break;
            start = p + 1;
        }
        p++;
    }
    *count = n;
    return 0;
}

static void execute_stage(const char *command) {
    char path[LINE_MAX];
    if (strcmp(command, "cat") == 0) copy_stream(0);
    else if (strncmp(command, "cat ", 4) == 0) cat_file(skip_spaces(command + 4));
    else if (strcmp(command, "ls") == 0) list_dir("/");
    else if (strncmp(command, "ls ", 3) == 0) list_dir(skip_spaces(command + 3));
    else if (strncmp(command, "echo ", 5) == 0) puts(command + 5);
    else {
        const char *name = command;
        size_t i = 0;
        if (*name != '/') path[i++] = '/';
        while (*name && *name != ' ' && i + 1 < sizeof(path)) path[i++] = *name++;
        path[i] = '\0';
        exec(path);
        printf("shell: cannot execute %s\n", path);
    }
}

static void run_pipeline(const char *line) {
    shell_stage_t stages[PIPELINE_MAX];
    int pipes[PIPELINE_MAX - 1][2];
    int pids[PIPELINE_MAX];
    int count;

    if (parse_pipeline(line, stages, &count) < 0) {
        puts("shell: invalid pipeline or redirection");
        return;
    }
    for (int i = 0; i + 1 < count; i++) {
        if (pipe(pipes[i]) < 0) { puts("shell: pipe failed"); return; }
    }
    for (int i = 0; i < count; i++) {
        int pid = fork();
        if (pid < 0) { puts("shell: fork failed"); pids[i] = -1; continue; }
        if (pid == 0) {
            if (i > 0) dup2(pipes[i - 1][0], 0);
            if (i + 1 < count) dup2(pipes[i][1], 1);
            if (stages[i].input[0]) {
                int fd = open(stages[i].input, O_RDONLY);
                if (fd < 0) { printf("shell: cannot open %s\n", stages[i].input); exit(1); }
                dup2(fd, 0); close(fd);
            }
            if (stages[i].output[0]) {
                int flags = O_WRONLY | O_CREAT | (stages[i].append ? O_APPEND : O_TRUNC);
                int fd = open(stages[i].output, flags);
                if (fd < 0) { printf("shell: cannot open %s\n", stages[i].output); exit(1); }
                dup2(fd, 1); close(fd);
            }
            for (int j = 0; j + 1 < count; j++) { close(pipes[j][0]); close(pipes[j][1]); }
            execute_stage(stages[i].command);
            exit(0);
        }
        pids[i] = pid;
    }
    for (int i = 0; i + 1 < count; i++) { close(pipes[i][0]); close(pipes[i][1]); }
    for (int i = 0; i < count; i++) if (pids[i] >= 0) wait(pids[i]);
}

static void run_command(const char *line) {
    if (line[0] == '\0') {
        return;
    }

    if (has_shell_operator(line)) {
        run_pipeline(line);
        return;
    }

    if (strcmp(line, "help") == 0) {
        puts("Commands: help, echo, about, clear, hello, memtest, yieldtest, sleeptest, faulttest, tasktest, fdtest, unixtest, cat, ls, taskinfo");
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

    if (strcmp(line, "unixtest") == 0) {
        int pid = spawn("/unixtest");
        if (pid < 0) printf("spawn failed: /unixtest\n");
        else printf("unixtest exited with status %d\n", wait(pid));
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
