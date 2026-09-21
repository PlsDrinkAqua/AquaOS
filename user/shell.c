#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zenos/readline.h>
#include <zenos/task.h>
#include <zenos/terminal.h>

#define LINE_MAX 128
#define PIPELINE_MAX 4

static int script_fd = -1;

#include "shell_completion.h"
#include "shell_highlight.h"

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

static int has_shell_operator(const char *line) {
    while (*line) {
        if (*line == '|' || *line == '<' || *line == '>') return 1;
        line++;
    }
    return 0;
}

static void copy_trimmed(char *dest, size_t size, const char *begin, const char *end) {
    size_t len;
    while (begin < end && (*begin == ' ' || *begin == '\t')) begin++;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) end--;
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
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            name_begin = p;
            while (p < end && *p != '<' && *p != '>') p++;
            name_end = p;
            while (name_end > name_begin && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
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

/* Whitespace-separated arguments; quoting and escapes are not supported yet. */
static void execute_stage(char *command) {
    const char *argv[33];
    char path[LINE_MAX];
    unsigned argc = 0;
    char *p = command;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (argc == 32) { puts("shell: too many arguments"); exit(1); }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    if (!argc) exit(0);
    unsigned i = 0;
    const char *name = argv[0];
    int has_slash = 0;
    for (const char *scan = name; *scan; scan++) if (*scan == '/') has_slash = 1;
    if (!has_slash) {
        const char *directories[] = {"/bin/", "/usr/bin/", "/"};
        for (unsigned dir = 0; dir < sizeof(directories) / sizeof(directories[0]); dir++) {
            if (strlen(directories[dir]) + strlen(name) >= sizeof(path)) {
                puts("shell: program path too long");
                exit(1);
            }
            strcpy(path, directories[dir]);
            strcpy(path + strlen(path), name);
            exec(path, argv);
        }
        printf("shell: command not found: %s\n", name);
        exit(127);
    }
    while (*name && i + 1 < sizeof(path)) path[i++] = *name++;
    if (*name) { puts("shell: program path too long"); exit(1); }
    path[i] = '\0';
    exec(path, argv);
    printf("shell: cannot execute %s\n", path);
    exit(127);
}

static int run_pipeline(const char *line) {
    shell_stage_t stages[PIPELINE_MAX];
    int pipes[PIPELINE_MAX - 1][2];
    int pids[PIPELINE_MAX];
    int count, status = 0, launch_failed = 0;

    if (parse_pipeline(line, stages, &count) < 0) {
        puts("shell: invalid pipeline or redirection");
        return 1;
    }
    if (zenos_terminal_begin_job() < 0) { puts("shell: terminal busy"); return 1; }
    for (int i = 0; i + 1 < count; i++) {
        if (pipe(pipes[i]) < 0) {
            puts("shell: pipe failed");
            for (int j = 0; j < i; j++) { close(pipes[j][0]); close(pipes[j][1]); }
            zenos_terminal_end_job();
            return 1;
        }
    }
    for (int i = 0; i < count; i++) {
        int pid = fork();
        if (pid < 0) { puts("shell: fork failed"); pids[i] = -1; launch_failed = 1; continue; }
        if (pid == 0) {
            if (script_fd >= 0) close(script_fd);
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
    for (int i = 0; i < count; i++) if (pids[i] >= 0) {
        int result = wait(pids[i]);
        if (result < 0) launch_failed = 1;
        if (i == count - 1) status = result < 0 ? 1 : result;
    }
    zenos_terminal_end_job();
    return launch_failed ? 1 : status;
}

static int run_command(const char *line) {
    if (line[0] == '\0') {
        return 0;
    }

    if (has_shell_operator(line)) {
        return run_pipeline(line);
    }

    if (strcmp(line, "cd") == 0 || strncmp(line, "cd ", 3) == 0 || strncmp(line, "cd\t", 3) == 0) {
        const char *path = line + 2;
        while (*path == ' ' || *path == '\t') path++;
        if (chdir(*path ? path : "/") < 0) {
            printf("cd: cannot change directory to %s\n", path);
            return 1;
        }
        return 0;
    }
    if (strcmp(line, "pwd") == 0) {
        char path[256];
        if (getcwd(path, sizeof(path))) puts(path);
        else { puts("pwd: cannot read working directory"); return 1; }
        return 0;
    }

    if (strcmp(line, "exit") == 0) {
        exit(0);
    }

    if (strcmp(line, "help") == 0) {
        puts("Commands: help, echo, about, clear, hello, cat, ls, mkdir, rm, rmdir, meinkampf, cd, pwd, taskinfo, exit");
        puts("Use exit or Ctrl+D to leave the shell; Ctrl+D ends cat input.");
        return 0;
    }

    if (strcmp(line, "clear") == 0) {
        zenos_terminal_clear();
        return 0;
    }

    if (strcmp(line, "about") == 0) {
        puts("AquaOS userland shell");
        return 0;
    }

    if (strcmp(line, "taskinfo") == 0) {
        zenos_taskinfo();
        return 0;
    }

    return run_pipeline(line);
}

/* Validate the whole AND-list before executing any part of it. */
static int run_line(char *line) {
    char *commands[LINE_MAX / 2];
    unsigned count = 0;
    for (char *p = line; *p; p++) {
        if (*p == '#' && (p == line || p[-1] == ' ' || p[-1] == '\t')) {
            *p = '\0';
            break;
        }
    }
    char *begin = line;
    for (char *p = line;; p++) {
        if (*p != '&' && *p) continue;
        int more = *p != '\0';
        if (more && p[1] != '&') { puts("shell: expected &&"); return 2; }
        char *end = p;
        while (begin < end && (*begin == ' ' || *begin == '\t')) begin++;
        while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if (begin == end) {
            if (!more && count == 0) return 0;
            puts("shell: empty command in && list");
            return 2;
        }
        *end = '\0';
        /* Check pipelines even in portions that might be skipped. */
        if (has_shell_operator(begin)) {
            shell_stage_t stages[PIPELINE_MAX];
            int stages_count;
            if (parse_pipeline(begin, stages, &stages_count) < 0) {
                puts("shell: invalid pipeline or redirection");
                return 2;
            }
        }
        commands[count++] = begin;
        if (!more) break;
        p++;
        begin = p + 1;
    }
    int status = 0;
    for (unsigned i = 0; i < count; i++) {
        status = run_command(commands[i]);
        if (status != 0) break;
    }
    return status;
}

static int run_script(const char *path) {
    FILE *script = fopen(path, "r");
    if (!script) { printf("shell: cannot open script %s\n", path); return 1; }
    script_fd = fileno(script);
    unsigned number = 1;
    int status = 0;
    for (;;) {
        char line[LINE_MAX];
        size_t length = 0;
        int c;
        while ((c = fgetc(script)) != EOF && c != '\n') {
            if (length + 1 == sizeof(line)) {
                printf("shell: %s:%u: line too long\n", path, number);
                status = 2;
                goto done;
            }
            if (c == 0) { puts("shell: NUL byte in script"); status = 2; goto done; }
            line[length++] = (char)c;
        }
        if (ferror(script)) { puts("shell: script read error"); status = 1; break; }
        if (c == EOF && !length) break;
        if (length && line[length - 1] == '\r') length--;
        line[length] = '\0';
        status = run_line(line);
        if (status) {
            printf("shell: %s:%u: stopped with status %d\n", path, number, status);
            break;
        }
        number++;
        if (c == EOF) break;
    }
done:
    if (fclose(script) < 0 && !status) status = 1;
    script_fd = -1;
    return status;
}

int main(int argc, char **argv) {
    char line[LINE_MAX];

    if (zenos_terminal_claim() < 0) { puts("shell: terminal busy"); return 1; }

    if (argc > 2) { puts("usage: shell [script]"); return 2; }
    if (argc == 2) return run_script(argv[1]);

    printf("AquaOS shell at %x\n", 0x08048000);

    for (;;) {
        write_str("\033[0;92mAquaOS\033[96m>\033[0m ");
        if (!zenos_readline_highlight(line, sizeof(line), complete_filename, highlight_line)) {
            putchar('\n');
            return 0;
        }
        run_line(line);
    }

}
