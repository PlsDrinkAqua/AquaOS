#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zenos/readline.h>
#include <zenos/task.h>
#include <zenos/terminal.h>

#define LINE_MAX 128
#define PIPELINE_MAX 4

extern void _start(void);

static int script_fd = -1;

#include "shell_completion.h"
#include "shell_highlight.h"

typedef struct {
    char command[LINE_MAX];
    char input[LINE_MAX];
    char output[LINE_MAX];
    int append;
    unsigned argc;
    unsigned offsets[32];
} shell_stage_t;

static void write_str(const char *s) {
    const char *p = s;

    while (*p) {
        p++;
    }

    write(1, s, (unsigned)(p - s));
}

#include "shell_parser.h"

static void execute_stage(shell_stage_t *stage) {
    const char *argv[33];
    char path[LINE_MAX];
    for (unsigned i = 0; i < stage->argc; i++)
        argv[i] = stage->command + stage->offsets[i];
    argv[stage->argc] = NULL;
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
            execute_stage(&stages[i]);
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

    shell_stage_t stages[PIPELINE_MAX];
    int count;
    if (parse_pipeline(line, stages, &count) < 0) {
        puts("shell: invalid command or expansion too long");
        return 2;
    }
    shell_stage_t *stage = &stages[0];
    if (count != 1 || stage->input[0] || stage->output[0]) return run_pipeline(line);
    const char *name = stage->command;
    if (strcmp(name, "cd") == 0) {
        if (stage->argc > 2) { puts("cd: too many arguments"); return 2; }
        const char *path = stage->argc == 2 ? stage->command + stage->offsets[1] : "/";
        if (chdir(path) < 0) {
            printf("cd: cannot change directory to %s\n", path);
            return 1;
        }
        return 0;
    }
    if (strcmp(name, "export") == 0 || strcmp(name, "unset") == 0) {
        int exporting = strcmp(name, "export") == 0;
        if (exporting && stage->argc == 1) {
            for (char **p = environ; p && *p; p++) puts(*p);
            return 0;
        }
        /* Validate every name before changing any values. */
        for (unsigned i = 1; i < stage->argc; i++) {
            char *arg = stage->command + stage->offsets[i];
            char *p = arg;
            if (!shell_name_start(*p++)) { puts("shell: invalid variable name"); return 2; }
            while (shell_name_char(*p)) p++;
            if (*p && !(exporting && *p == '=')) { puts("shell: invalid variable name"); return 2; }
        }
        for (unsigned i = 1; i < stage->argc; i++) {
            char *arg = stage->command + stage->offsets[i];
            char *eq = arg;
            while (*eq && *eq != '=') eq++;
            const char *value = "";
            if (*eq) { *eq = 0; value = eq + 1; }
            else if (getenv(arg)) value = getenv(arg);
            int result = exporting ? setenv(arg, value, 1) : unsetenv(arg);
            if (result < 0) { puts("shell: cannot update environment"); return 1; }
        }
        return 0;
    }
    /* Other builtins accept no arguments. */
    if (stage->argc == 1 && strcmp(name, "pwd") == 0) {
        char path[256];
        if (getcwd(path, sizeof(path))) puts(path);
        else { puts("pwd: cannot read working directory"); return 1; }
        return 0;
    }

    if (stage->argc == 1 && strcmp(name, "exit") == 0) {
        exit(0);
    }

    if (stage->argc == 1 && strcmp(name, "help") == 0) {
        puts("Commands: help, echo, about, clear, hello, cat, cp, mv, ls, mkdir, rm, rmdir, kilo, cd, pwd, taskinfo, exit");
        puts("Use exit or Ctrl+D to leave the shell; Ctrl+D ends cat input.");
        return 0;
    }

    if (stage->argc == 1 && strcmp(name, "clear") == 0) {
        zenos_terminal_clear();
        return 0;
    }

    if (stage->argc == 1 && strcmp(name, "about") == 0) {
        puts("AquaOS userland shell");
        return 0;
    }

    if (stage->argc == 1 && strcmp(name, "taskinfo") == 0) {
        zenos_taskinfo();
        return 0;
    }

    return run_pipeline(line);
}

/* Validate the whole AND-list before executing any part of it. */
static int run_line_internal(char *line) {
    char *commands[LINE_MAX / 2];
    unsigned count = 0;
    char *begin = line;
    const char *scan = line;
    char word[LINE_MAX];
    for (;;) {
        const char *token_start = scan;
        while (*token_start == ' ' || *token_start == '\t') token_start++;
        int kind = shell_token(&scan, word);
        if (kind < 0) { puts("shell: invalid quoting, escape, or operator"); return 2; }
        if (kind != SH_AND && kind != SH_END) continue;
        char *end = (char *)token_start;
        int more = kind == SH_AND;
        while (begin < end && (*begin == ' ' || *begin == '\t')) begin++;
        if (begin == end) {
            if (!more && count == 0) return shell_last_status;
            puts("shell: empty command in && list");
            return 2;
        }
        *end = '\0';
        shell_stage_t stages[PIPELINE_MAX];
        int stages_count;
        if (parse_pipeline(begin, stages, &stages_count) < 0) {
            puts("shell: invalid pipeline or redirection");
            return 2;
        }
        commands[count++] = begin;
        if (!more) break;
        begin = (char *)scan;
    }
    shell_expand = 1;
    int status = shell_last_status;
    for (unsigned i = 0; i < count; i++) {
        status = run_command(commands[i]);
        shell_last_status = status;
        if (status != 0) break;
    }
    return status;
}

static int run_line(char *line) {
    shell_expand = 0;
    int status = run_line_internal(line);
    shell_expand = 1;
    shell_last_status = status;
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

    printf("AquaOS shell at %p\n", (void *)_start);

    for (;;) {
        write_str("\033[0;92mAquaOS\033[96m>\033[0m ");
        if (!zenos_readline_highlight(line, sizeof(line), complete_filename, highlight_line)) {
            putchar('\n');
            return 0;
        }
        run_line(line);
    }

}
