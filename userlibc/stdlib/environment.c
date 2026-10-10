#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

char **environ;
static char **owned;

static int valid_name(const char *name) {
    if (!name || !*name) return 0;
    for (const char *p = name; *p; p++) if (*p == '=') return 0;
    return 1;
}

char *getenv(const char *name) {
    if (!valid_name(name)) return NULL;
    size_t n = strlen(name);
    for (char **p = environ; p && *p; p++)
        if (!strncmp(*p, name, n) && (*p)[n] == '=') return *p + n + 1;
    return NULL;
}

static int change_environment(const char *name, const char *value) {
    size_t n = strlen(name), count = 0, kept = 0;
    while (environ && environ[count]) count++;
    char **next = calloc(count + 2, sizeof(*next));
    if (!next) { errno = ENOMEM; return -1; }
    for (size_t i = 0; i < count; i++) {
        if (!strncmp(environ[i], name, n) && environ[i][n] == '=') continue;
        size_t length = strlen(environ[i]) + 1;
        next[kept] = malloc(length);
        if (!next[kept]) goto fail;
        memcpy(next[kept++], environ[i], length);
    }
    if (value) {
        size_t v = strlen(value);
        if (n > (size_t)-1 - v - 2) goto fail;
        next[kept] = malloc(n + v + 2);
        if (!next[kept]) goto fail;
        memcpy(next[kept], name, n);
        next[kept][n] = '=';
        memcpy(next[kept++] + n + 1, value, v + 1);
    }
    if (owned) {
        for (size_t i = 0; owned[i]; i++) free(owned[i]);
        free(owned);
    }
    owned = environ = next;
    return 0;
fail:
    for (size_t i = 0; i < kept; i++) free(next[i]);
    free(next);
    errno = ENOMEM;
    return -1;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!valid_name(name) || !value) { errno = EINVAL; return -1; }
    if (!overwrite && getenv(name)) return 0;
    return change_environment(name, value);
}

int unsetenv(const char *name) {
    if (!valid_name(name)) { errno = EINVAL; return -1; }
    if (!getenv(name)) return 0;
    return change_environment(name, NULL);
}
