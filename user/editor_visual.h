/* Visual editor shares the document loader and transactional save with line mode. */
#define EDIT_ROWS 23U
#define EDIT_COLS 79U
#define HISTORY_MAX 32U

typedef struct {
    char *text;
    unsigned size, cursor;
} snapshot_t;

typedef struct {
    document_t doc;
    unsigned cursor, top, left;
    int insert, pending, recording;
    snapshot_t undo[HISTORY_MAX], redo[HISTORY_MAX];
    unsigned undos, redos;
    char *yank, *saved;
    unsigned yank_size, saved_size;
    int yank_line;
    char message[COMMAND_MAX], search[COMMAND_MAX];
} editor_t;

static unsigned bol(editor_t *e, unsigned p) {
    while (p && e->doc.text[p - 1] != '\n') p--;
    return p;
}

static unsigned eol(editor_t *e, unsigned p) {
    while (p < e->doc.size && e->doc.text[p] != '\n') p++;
    return p;
}

static void normal_cursor(editor_t *e) {
    if (e->cursor > e->doc.size) e->cursor = e->doc.size;
    if (!e->insert && e->cursor > bol(e, e->cursor) &&
        e->cursor == eol(e, e->cursor)) e->cursor--;
}

static void history_clear(snapshot_t *history, unsigned *count) {
    while (*count) free(history[--*count].text);
}

static int history_push(editor_t *e, snapshot_t *history, unsigned *count) {
    char *text = malloc(e->doc.size + 1);
    if (!text) { strcpy(e->message, "Out of memory; edit cancelled"); return 0; }
    memcpy(text, e->doc.text, e->doc.size);
    if (*count == HISTORY_MAX) {
        free(history[0].text);
        memmove(history, history + 1, (HISTORY_MAX - 1) * sizeof(*history));
        (*count)--;
    }
    history[(*count)++] = (snapshot_t){text, e->doc.size, e->cursor};
    return 1;
}

static void dirty_update(editor_t *e) {
    e->doc.dirty = e->doc.size != e->saved_size ||
        memcmp(e->doc.text, e->saved, e->doc.size);
}

static int change(editor_t *e, unsigned begin, unsigned end, const char *text, unsigned n) {
    unsigned size = e->doc.size - (end - begin) + n;
    if (size > DOCUMENT_MAX) { strcpy(e->message, "File too large; edit cancelled"); return 0; }
    if (begin == end && !n) return 1;
    if (!e->recording) {
        if (!history_push(e, e->undo, &e->undos)) return 0;
        history_clear(e->redo, &e->redos);
        if (e->insert) e->recording = 1;
    }
    memmove(e->doc.text + begin + n, e->doc.text + end, e->doc.size - end);
    if (n) memcpy(e->doc.text + begin, text, n);
    e->doc.size = size;
    e->cursor = begin;
    dirty_update(e);
    return 1;
}

static void undo_redo(editor_t *e, int redo) {
    snapshot_t *from = redo ? e->redo : e->undo, *to = redo ? e->undo : e->redo;
    unsigned *src = redo ? &e->redos : &e->undos, *dst = redo ? &e->undos : &e->redos;
    if (!*src) { strcpy(e->message, redo ? "Already at newest change" : "Already at oldest change"); return; }
    if (!history_push(e, to, dst)) return;
    snapshot_t s = from[--*src];
    memcpy(e->doc.text, s.text, s.size);
    e->doc.size = s.size;
    e->cursor = s.cursor;
    free(s.text);
    dirty_update(e);
    normal_cursor(e);
}

static unsigned column(editor_t *e, unsigned start, unsigned end) {
    unsigned col = 0;
    for (; start < end; start++) col += e->doc.text[start] == '\t' ? 4 - col % 4 : 1;
    return col;
}

static void vertical(editor_t *e, int down) {
    unsigned start = bol(e, e->cursor), col = e->cursor - start, target;
    if (down) {
        target = eol(e, start);
        if (target == e->doc.size) return;
        target++;
    } else {
        if (!start) return;
        target = bol(e, start - 1);
    }
    unsigned end = eol(e, target);
    e->cursor = col < end - target ? target + col : end;
    normal_cursor(e);
}

static void display(editor_t *e) {
    unsigned row = 0, start = bol(e, e->cursor);
    for (unsigned i = 0; i < start; i++) if (e->doc.text[i] == '\n') row++;
    if (row < e->top) e->top = row;
    if (row >= e->top + EDIT_ROWS) e->top = row - EDIT_ROWS + 1;
    unsigned col = column(e, start, e->cursor);
    if (col < e->left) e->left = col;
    if (col >= e->left + EDIT_COLS) e->left = col - EDIT_COLS + 1;
    unsigned p = line_start(&e->doc, e->top + 1);
    for (unsigned y = 0; y < EDIT_ROWS; y++) {
        printf("\033[%u;1H\033[0m\033[K", y + 1);
        if (p > e->doc.size) { putchar('~'); continue; }
        unsigned end = eol(e, p), x = 0;
        for (unsigned i = p; i <= end; i++) {
            unsigned width = i < end && e->doc.text[i] == '\t' ? 4 - x % 4 : 1;
            for (unsigned k = 0; k < width; k++, x++) {
                if (x < e->left || x >= e->left + EDIT_COLS) continue;
                int cursor = i == e->cursor && !k;
                if (cursor) printf("\033[7m");
                unsigned char c = i == end || e->doc.text[i] == '\t' ? ' ' : (unsigned char)e->doc.text[i];
                putchar(c >= 32 && c < 127 ? c : '?');
                if (cursor) printf("\033[0m");
            }
            if (x >= e->left + EDIT_COLS) break;
        }
        p = end + 1;
    }
    char status[80];
    snprintf(status, sizeof(status), " %s  %.45s%s  %u:%u", e->insert ? "INSERT" : "NORMAL",
             e->doc.path, e->doc.dirty ? " [+]" : "", row + 1, col + 1);
    printf("\033[24;1H\033[7m%-79.79s\033[0m\033[25;1H\033[K%.79s", status, e->message);
    fflush(stdout);
}

static int key(void) {
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}

static int prompt(editor_t *e, char prefix, char *text) {
    unsigned n = 0, excess = 0;
    for (;;) {
        text[n] = 0;
        printf("\033[25;1H\033[K%c%.76s", prefix, text + (n > 76 ? n - 76 : 0));
        fflush(stdout);
        int c = key();
        if (c < 0) return -1;
        if (c == 27) return 0;
        if (c == '\n' || c == '\r') {
            if (excess) { strcpy(e->message, "Command too long; ignored"); return 0; }
            return 1;
        }
        if (c == '\b' || c == 127) {
            if (excess) excess--;
            else if (n) n--;
            else return 0;
        } else if (c >= 32 && c < 127) {
            if (n + 1 < COMMAND_MAX && !excess) text[n++] = c;
            else excess++;
        }
    }
}

static void find_next(editor_t *e, int backwards) {
    unsigned n = strlen(e->search), size = e->doc.size;
    if (!n || n > size) { strcpy(e->message, "Pattern not found"); return; }
    for (unsigned step = 1; step <= size; step++) {
        unsigned p = backwards ? (e->cursor + size - step) % size : (e->cursor + step) % size;
        if (p + n <= size && !memcmp(e->doc.text + p, e->search, n)) {
            e->cursor = p;
            return;
        }
    }
    strcpy(e->message, "Pattern not found");
}

static int word_class(char c) {
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') return 0;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' ? 1 : 2;
}

static unsigned word_next(editor_t *e, unsigned p) {
    if (p < e->doc.size) {
        int kind = word_class(e->doc.text[p]);
        while (p < e->doc.size && word_class(e->doc.text[p]) == kind) p++;
        while (p < e->doc.size && !word_class(e->doc.text[p])) p++;
    }
    return p;
}

static void yank_range(editor_t *e, unsigned begin, unsigned end, int linewise) {
    memcpy(e->yank, e->doc.text + begin, end - begin);
    e->yank_size = end - begin;
    e->yank_line = linewise;
}

static int ex_command(editor_t *e, char *cmd) {
    if (!strcmp(cmd, "q!")) return 1;
    if (!strcmp(cmd, "w") || !strcmp(cmd, "wq") || !strcmp(cmd, "x")) {
        if (save(&e->doc) < 0) { strcpy(e->message, "Save failed; buffer retained (check path, space, or .mk-tmp file)"); return 0; }
        memcpy(e->saved, e->doc.text, e->doc.size);
        e->saved_size = e->doc.size;
        snprintf(e->message, sizeof(e->message), "Saved %u bytes", e->doc.size);
        return strcmp(cmd, "w") != 0;
    }
    if (!strcmp(cmd, "q")) {
        if (!e->doc.dirty) return 1;
        strcpy(e->message, "Unsaved changes; use :w or :q!");
    } else if (!strcmp(cmd, "redo")) undo_redo(e, 1);
    else {
        unsigned number = 0;
        const char *p = cmd;
        while (*p >= '0' && *p <= '9' && number <= DOCUMENT_MAX) number = number * 10 + (*p++ - '0');
        if (number && number <= DOCUMENT_MAX && !*p) {
            e->cursor = line_start(&e->doc, number);
            normal_cursor(e);
        } else strcpy(e->message, "Unknown command");
    }
    return 0;
}

static void normal_key(editor_t *e, int c) {
    unsigned start = bol(e, e->cursor), end = eol(e, e->cursor);
    int pending = e->pending;
    e->pending = 0;
    if (pending) {
        if (pending == 'g' && c == 'g') e->cursor = 0;
        else if ((pending == 'd' || pending == 'y') && (c == pending || c == 'w' || c == '$')) {
            unsigned a = c == pending ? start : e->cursor;
            unsigned b = c == pending ? end + (end < e->doc.size) : c == 'w' ? word_next(e, a) : end;
            yank_range(e, a, b, c == pending);
            if (pending == 'd') change(e, a, b, NULL, 0);
        }
        normal_cursor(e);
        return;
    }
    switch (c) {
    case 'h': if (e->cursor > start) e->cursor--; break;
    case 'l': if (e->cursor < end) e->cursor++; break;
    case 'j': vertical(e, 1); break;
    case 'k': vertical(e, 0); break;
    case '0': e->cursor = start; break;
    case '^':
        e->cursor = start;
        while (e->cursor < end && (e->doc.text[e->cursor] == ' ' || e->doc.text[e->cursor] == '\t')) e->cursor++;
        break;
    case '$': e->cursor = end; break;
    case 'w': e->cursor = word_next(e, e->cursor); break;
    case 'b':
        if (e->cursor) e->cursor--;
        while (e->cursor && !word_class(e->doc.text[e->cursor])) e->cursor--;
        while (e->cursor && word_class(e->doc.text[e->cursor - 1]) == word_class(e->doc.text[e->cursor])) e->cursor--;
        break;
    case 'G': e->cursor = bol(e, e->doc.size && e->doc.text[e->doc.size - 1] == '\n' ? e->doc.size - 1 : e->doc.size); break;
    case 'g': case 'd': case 'y': e->pending = c; break;
    case 'i': case 'a': case 'I': case 'A':
        e->insert = 1;
        if (c == 'a' && e->cursor < end) e->cursor++;
        if (c == 'A') e->cursor = end;
        if (c == 'I') {
            e->cursor = start;
            while (e->cursor < end && (e->doc.text[e->cursor] == ' ' || e->doc.text[e->cursor] == '\t')) e->cursor++;
        }
        break;
    case 'o': case 'O': {
        unsigned p = c == 'O' ? start : end;
        e->insert = 1;
        if (change(e, p, p, "\n", 1) && c == 'o') e->cursor++;
        break;
    }
    case 'x': case 'D':
        if (e->cursor < end) {
            unsigned b = c == 'x' ? e->cursor + 1 : end;
            yank_range(e, e->cursor, b, 0);
            change(e, e->cursor, b, NULL, 0);
        }
        break;
    case 'p': case 'P':
        if (e->yank_size) {
            unsigned p = e->yank_line ? (c == 'P' ? start : end + (end < e->doc.size)) : e->cursor + (c == 'p' && e->cursor < end);
            /* Normalize linewise registers when copying an unterminated last line. */
            unsigned n = e->yank_size;
            if (e->yank_line && e->yank[n - 1] != '\n') e->yank[n++] = '\n';
            if (e->yank_line && p && e->doc.text[p - 1] != '\n') {
                memmove(e->yank + 1, e->yank, n);
                e->yank[0] = '\n';
                change(e, p, p, e->yank, n + 1);
                memmove(e->yank, e->yank + 1, n);
                if (e->cursor == p && e->doc.size > p) e->cursor++;
            } else change(e, p, p, e->yank, n);
        }
        break;
    case 'u': undo_redo(e, 0); break;
    case 'n': find_next(e, 0); break;
    case 'N': find_next(e, 1); break;
    }
    normal_cursor(e);
}

static int visual_main(int argc, char **argv) {
    if (argc != 2) { puts("usage: meinkampf [--line] <file>"); return 1; }
    editor_t *e = calloc(1, sizeof(*e));
    if (!e) return 1;
    e->doc.path = argv[1];
    e->doc.text = malloc(DOCUMENT_MAX);
    e->yank = malloc(DOCUMENT_MAX + 2);
    e->saved = malloc(DOCUMENT_MAX);
    int status = 1;
    if (!e->doc.text || !e->yank || !e->saved) { puts("editor: out of memory"); goto done; }
    if (load(&e->doc) < 0) { printf("editor: cannot load %s (errno=%d)\n", e->doc.path, errno); goto done; }
    memcpy(e->saved, e->doc.text, e->doc.size);
    e->saved_size = e->doc.size;
    printf("\033[0m\033[2J\033[H");
    status = 0;
    for (;;) {
        display(e);
        int c = key();
        e->message[0] = 0;
        if (c < 0) { status = e->doc.dirty ? 1 : 0; break; }
        if (c >= ZENOS_KEY_UP && c <= ZENOS_KEY_RIGHT) {
            e->pending = 0;
            /* Moving in Insert mode starts a separate undo group. */
            e->recording = 0;
            if (c == ZENOS_KEY_UP) vertical(e, 0);
            if (c == ZENOS_KEY_DOWN) vertical(e, 1);
            if (c == ZENOS_KEY_LEFT && e->cursor > bol(e, e->cursor)) e->cursor--;
            if (c == ZENOS_KEY_RIGHT && e->cursor < eol(e, e->cursor)) e->cursor++;
            normal_cursor(e);
        } else if (e->insert) {
            if (c == 27) {
                e->insert = e->recording = 0;
                if (e->cursor > bol(e, e->cursor)) e->cursor--;
            } else if (c == '\b' || c == 127) {
                if (e->cursor) change(e, e->cursor - 1, e->cursor, NULL, 0);
            } else if (c == '\n' || c == '\t' || (c >= 32 && c < 127)) {
                char ch = c;
                if (change(e, e->cursor, e->cursor, &ch, 1)) e->cursor++;
            }
        } else if ((c == ':' || c == '/') && !e->pending) {
            char command[COMMAND_MAX];
            int result = prompt(e, c, command);
            if (result < 0) { status = e->doc.dirty ? 1 : 0; break; }
            if (result) {
                if (c == ':' && ex_command(e, command)) break;
                if (c == '/') {
                    if (*command) strcpy(e->search, command);
                    find_next(e, 0);
                }
            }
        } else normal_key(e, c);
    }
    printf("\033[0m\033[2J\033[H");
    if (status) puts("Input ended; unsaved edits were not written");
done:
    history_clear(e->undo, &e->undos);
    history_clear(e->redo, &e->redos);
    free(e->doc.text); free(e->yank); free(e->saved); free(e);
    return status;
}
