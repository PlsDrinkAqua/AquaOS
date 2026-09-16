#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zenos/keys.h>

#define DOCUMENT_MAX 65536U
#define COMMAND_MAX 512U

typedef struct {
    char *text;
    unsigned size;
    int dirty;
    const char *path;
} document_t;

/* Unlike the shell's readline, reject overlong input instead of truncating it. */
static int input(char *line) {
    unsigned length = 0, excess = 0;
    for (;;) {
        char c;
        int n = read(0, &c, 1);
        if (n < 0) return -1;
        if (n && (unsigned char)c >= ZENOS_KEY_UP && (unsigned char)c <= ZENOS_KEY_RIGHT)
            continue;
        if (!n || c == 4 || c == '\n') {
            line[length] = 0;
            if (n) putchar('\n');
            if (excess) { puts("input too long; command ignored"); return -2; }
            return length || (n && c == '\n') ? 1 : 0;
        }
        if (c == '\b') {
            if (excess) excess--;
            else if (length) { length--; putchar('\b'); }
        } else if (!c) {
            excess++; /* Embedded NUL cannot silently turn into another command. */
        } else if (length + 1 < COMMAND_MAX && !excess) {
            line[length++] = c;
            putchar(c);
        } else excess++;
    }
}

static unsigned lines(const document_t *doc) {
    unsigned count = 0;
    for (unsigned i = 0; i < doc->size; i++) if (doc->text[i] == '\n') count++;
    return count + (doc->size && doc->text[doc->size - 1] != '\n');
}

static unsigned line_start(const document_t *doc, unsigned number) {
    unsigned pos = 0;
    while (number > 1 && pos < doc->size) if (doc->text[pos++] == '\n') number--;
    return pos;
}

static int load(document_t *doc) {
    int fd = open(doc->path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) { puts("new file"); return 0; }
        return -1;
    }
    char chunk[256];
    for (;;) {
        int n = read(fd, chunk, sizeof(chunk));
        if (n < 0) { int error = errno; close(fd); errno = error; return -1; }
        if (!n) break;
        if (doc->size + (unsigned)n > DOCUMENT_MAX) { close(fd); errno = EFBIG; return -1; }
        for (int i = 0; i < n; i++) {
            if (!chunk[i]) { close(fd); errno = EINVAL; return -1; }
            doc->text[doc->size++] = chunk[i];
        }
    }
    return close(fd);
}

static void print(const document_t *doc) {
    unsigned number = 1;
    for (unsigned pos = 0; pos < doc->size;) {
        printf("%u: ", number++);
        do { putchar(doc->text[pos]); } while (doc->text[pos++] != '\n' && pos < doc->size);
    }
    if (doc->size && doc->text[doc->size - 1] != '\n') puts("\n[no final newline]");
    if (!doc->size) puts("[empty]");
}

static void edit(document_t *doc, char op, unsigned number, const char *text) {
    unsigned count = lines(doc);
    if (!number || number > count + (op == 'i')) { puts("invalid line number"); return; }
    unsigned begin = line_start(doc, number), end = begin;
    if (op != 'i') {
        while (end < doc->size && doc->text[end++] != '\n') {}
    }
    unsigned length = op == 'd' ? 0 : strlen(text);
    unsigned separator = op == 'i' && begin == doc->size && doc->size &&
                         doc->text[doc->size - 1] != '\n';
    unsigned added = op == 'd' ? 0 : length + 1 + separator;
    unsigned size = doc->size - (end - begin) + added;
    if (size > DOCUMENT_MAX) { puts("file too large; edit ignored"); return; }
    if (added > end - begin) {
        for (unsigned i = doc->size; i > end; i--) doc->text[begin + added + i - end - 1] = doc->text[i - 1];
    } else {
        for (unsigned i = end; i < doc->size; i++) doc->text[begin + added + i - end] = doc->text[i];
    }
    if (op != 'd') {
        if (separator) doc->text[begin++] = '\n';
        for (unsigned i = 0; i < length; i++) doc->text[begin + i] = text[i];
        doc->text[begin + length] = '\n';
    }
    doc->size = size;
    doc->dirty = 1;
}

static int save(document_t *doc) {
    char temporary[256];
    const char *suffix = ".mk-tmp";
    unsigned length = strlen(doc->path), extra = strlen(suffix);
    if (length + extra >= sizeof(temporary)) { puts("save failed: path too long"); return -1; }
    for (unsigned i = 0; i < length; i++) temporary[i] = doc->path[i];
    for (unsigned i = 0; i <= extra; i++) temporary[length + i] = suffix[i];
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL);
    if (fd < 0) { printf("save failed: %s (errno=%d)\n", temporary, errno); return -1; }
    unsigned written = 0;
    while (written < doc->size) {
        int n = write(fd, doc->text + written, doc->size - written);
        if (n <= 0) {
            int error = n < 0 ? errno : EIO;
            close(fd); unlink(temporary);
            printf("save failed (errno=%d); buffer retained\n", error);
            return -1;
        }
        written += n;
    }
    if (close(fd) < 0) { puts("save failed: close; temporary file retained"); return -1; }
    if (rename(temporary, doc->path) < 0) {
        printf("save failed (errno=%d); recovery file: %s\n", errno, temporary);
        return -1;
    }
    doc->dirty = 0;
    printf("saved %u bytes\n", doc->size);
    return 0;
}

static void help(void) {
    puts("p: print, a text: append, s: save, q: quit, q!: discard, h: help");
    puts("i 1 hello: insert before line 1; r 1 hello: replace line 1");
    puts("d 1: delete line 1. Use an actual line number, starting at 1.");
}

static int line_main(int argc, char **argv) {
    if (argc != 2) { puts("usage: meinkampf <file>"); return 1; }
    document_t doc = { .path = argv[1] };
    doc.text = malloc(DOCUMENT_MAX);
    if (!doc.text) { puts("meinkampf: out of memory"); return 1; }
    if (load(&doc) < 0) {
        printf("meinkampf: cannot load %s (errno=%d)\n", doc.path, errno);
        free(doc.text); return 1;
    }
    printf("meinkampf: %s (%u lines)\n", doc.path, lines(&doc));
    help();
    char command[COMMAND_MAX];
    int status = 0;
    for (;;) {
        printf("meinkampf%s> ", doc.dirty ? "*" : "");
        int n = input(command);
        if (n == -2) continue;
        if (n < 0) { puts("input error; unsaved edits were not written"); status = 1; break; }
        if (n == 0 || !strcmp(command, "q")) {
            if (!doc.dirty) break;
            printf("Discard unsaved changes? y/N: ");
            int answer = input(command);
            if (answer > 0 && !strcmp(command, "y")) break;
            if (answer == 0 || answer == -1) { puts("input ended; unsaved edits were not written"); status = 1; break; }
            continue;
        }
        if (!strcmp(command, "q!")) break;
        if (!strcmp(command, "p")) { print(&doc); continue; }
        if (!strcmp(command, "s") || !strcmp(command, "w")) { save(&doc); continue; }
        if (!strcmp(command, "h")) {
            help();
            continue;
        }
        char op = command[0];
        if (op == 'a' && (!command[1] || command[1] == ' ')) {
            edit(&doc, 'i', lines(&doc) + 1, command[1] ? command + 2 : "");
            continue;
        }
        if ((op == 'i' || op == 'r' || op == 'd') && command[1] == ' ') {
            const char *p = command + 2;
            unsigned number = 0;
            while (*p == ' ') p++;
            while (*p >= '0' && *p <= '9' && number <= DOCUMENT_MAX) number = number * 10 + (*p++ - '0');
            if (number && number <= DOCUMENT_MAX && (!*p || (op != 'd' && *p == ' '))) {
                edit(&doc, op, number, *p ? p + 1 : p);
                continue;
            }
        }
        if (op == 'i' || op == 'r')
            printf("use %c <line number> <text>, for example: %c 1 hello\n", op, op);
        else if (op == 'd') puts("use d <line number>, for example: d 1");
        else puts("invalid command; h for help");
    }
    free(doc.text);
    return status;
}

#include "editor_visual.h"

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--line"))
        return line_main(argc - 1, argv + 1);
    return visual_main(argc, argv);
}
