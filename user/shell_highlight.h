/* Keep shell token classification outside the generic line editor. */
static int highlight_program(const char *path) {
    unsigned char magic[4];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    int n = read(fd, magic, sizeof(magic));
    close(fd);
    return n == 4 && magic[0] == 0x7f && magic[1] == 'E' &&
           magic[2] == 'L' && magic[3] == 'F';
}

static const char *highlight_command(const char *word) {
    static const char *builtins[] = {
        "cd", "pwd", "exit", "help", "clear", "about", "taskinfo"
    };
    static const char *directories[] = {"/bin/", "/usr/bin/", "/"};
    for (unsigned i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
        if (!strcmp(word, builtins[i])) return "\033[96m";
    if (strchr(word, '/'))
        return highlight_program(word) ? "\033[95m" : "\033[91m";
    for (unsigned i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        char path[LINE_MAX + 16];
        strcpy(path, directories[i]);
        strcpy(path + strlen(path), word);
        if (highlight_program(path)) return "\033[95m";
    }
    return "\033[91m";
}

static void highlight_line(const char *line) {
    int command = 1;
    size_t i = 0;
    while (line[i]) {
        size_t start = i;
        const char *color = "\033[0m";
        if (line[i] == '#' && (!i || line[i - 1] == ' ' || line[i - 1] == '\t')) {
            color = "\033[90m";
            i += strlen(line + i);
        } else if (line[i] == ' ' || line[i] == '\t') {
            i++;
        } else if (strchr("|&<>", line[i])) {
            char op = line[i++];
            color = "\033[93m";
            if ((op == '&' || op == '>') && line[i] == op) i++;
            command = op == '|' || op == '&';
        } else {
            while (line[i] && !completion_separator(line[i])) i++;
            if (command) {
                char word[LINE_MAX];
                memcpy(word, line + start, i - start);
                word[i - start] = '\0';
                color = highlight_command(word);
                command = 0;
            }
        }
        fputs(color, stdout);
        fwrite(line + start, 1, i - start, stdout);
    }
    fputs("\033[0m", stdout);
}
