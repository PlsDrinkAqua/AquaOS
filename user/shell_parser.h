/* Shared lexer for command arguments, operators, and AND-list validation. */
enum { SH_END, SH_WORD, SH_PIPE, SH_IN, SH_OUT, SH_APPEND, SH_AND };

static int shell_last_status;
static int shell_expand = 1;

static int shell_append(char *word, unsigned *n, const char *text) {
    while (*text) {
        if (*n + 1 >= LINE_MAX) return -1;
        word[(*n)++] = *text++;
    }
    return 0;
}

static int shell_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int shell_name_char(char c) {
    return shell_name_start(c) || (c >= '0' && c <= '9');
}

static int shell_variable(const char **cursor, char *word, unsigned *n) {
    const char *start = *cursor, *p = start + 1;
    char name[LINE_MAX], status[16];
    unsigned length = 0;
    const char *value;
    if (*p == '?') {
        p++;
        snprintf(status, sizeof(status), "%d", shell_last_status);
        value = status;
    } else {
        int braces = *p == '{';
        if (braces) p++;
        if (!shell_name_start(*p)) {
            if (braces) return -1;
            *cursor = p;
            return shell_append(word, n, "$");
        }
        while (shell_name_char(*p)) name[length++] = *p++;
        name[length] = 0;
        if (braces && *p++ != '}') return -1;
        value = getenv(name);
        if (!value) value = "";
    }
    *cursor = p;
    if (!shell_expand) {
        while (start < p) {
            if (*n + 1 >= LINE_MAX) return -1;
            word[(*n)++] = *start++;
        }
        return 0;
    }
    return shell_append(word, n, value);
}

static int shell_token(const char **cursor, char word[LINE_MAX]) {
    const char *p = *cursor;
    unsigned n = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '#') { *cursor = p + strlen(p); return SH_END; }
    if (*p == '|' || *p == '<' || *p == '>' || *p == '&') {
        char c = *p++;
        int kind = c == '|' ? SH_PIPE : c == '<' ? SH_IN : SH_OUT;
        if (c == '>' && *p == '>') { p++; kind = SH_APPEND; }
        if (c == '&') {
            if (*p != '&') return -1;
            p++; kind = SH_AND;
        }
        *cursor = p;
        return kind;
    }
    while (*p && *p != ' ' && *p != '\t' && *p != '|' && *p != '<' && *p != '>' && *p != '&') {
        if (*p == '\'' || *p == '"') {
            char quote = *p++;
            while (*p && *p != quote) {
                if (quote == '"' && *p == '$') {
                    if (shell_variable(&p, word, &n) < 0) return -1;
                    continue;
                }
                if (quote == '"' && *p == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '$' || p[1] == '`')) p++;
                if (n + 1 >= LINE_MAX) return -1;
                word[n++] = *p++;
            }
            if (!*p) return -1;
            p++;
        } else {
            if (*p == '$') {
                if (shell_variable(&p, word, &n) < 0) return -1;
                continue;
            }
            if (*p == '\\') { p++; if (!*p) return -1; }
            if (n + 1 >= LINE_MAX) return -1;
            word[n++] = *p++;
        }
    }
    word[n] = '\0';
    *cursor = p;
    return SH_WORD;
}

static int parse_pipeline(const char *line, shell_stage_t stages[], int *count) {
    const char *p = line;
    char word[LINE_MAX];
    int n = 0;
    size_t used = 0;
    memset(&stages[0], 0, sizeof(stages[0]));
    for (;;) {
        int kind = shell_token(&p, word);
        shell_stage_t *stage = &stages[n];
        if (kind < 0 || kind == SH_AND) return -1;
        if (kind == SH_END || kind == SH_PIPE) {
            if (!stage->argc) return -1;
            if (kind == SH_END) { *count = n + 1; return 0; }
            if (++n == PIPELINE_MAX) return -1;
            memset(&stages[n], 0, sizeof(stages[n]));
            used = 0;
        } else if (kind == SH_WORD) {
            size_t length = strlen(word) + 1;
            if (stage->argc == 32 || used + length > sizeof(stage->command)) return -1;
            stage->offsets[stage->argc++] = (unsigned)used;
            memcpy(stage->command + used, word, length);
            used += length;
        } else {
            if (shell_token(&p, word) != SH_WORD || !word[0]) return -1;
            if (kind == SH_IN) {
                if (stage->input[0]) return -1;
                strcpy(stage->input, word);
            } else {
                if (stage->output[0]) return -1;
                strcpy(stage->output, word);
                stage->append = kind == SH_APPEND;
            }
        }
    }
}
