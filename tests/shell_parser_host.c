#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LINE_MAX 128
#define PIPELINE_MAX 4
typedef struct {
    char command[LINE_MAX], input[LINE_MAX], output[LINE_MAX];
    int append;
    unsigned argc, offsets[32];
} shell_stage_t;
#include "../user/shell_parser.h"

int main(void) {
    shell_stage_t s[PIPELINE_MAX];
    int n;
    assert(!parse_pipeline("echo a\"b c\"d '' \\| '#' > 'my file' | cat", s, &n));
    assert(n == 2 && s[0].argc == 5);
    assert(!strcmp(s[0].command + s[0].offsets[1], "ab cd"));
    assert(!strcmp(s[0].command + s[0].offsets[2], ""));
    assert(!strcmp(s[0].command + s[0].offsets[3], "|"));
    assert(!strcmp(s[0].command + s[0].offsets[4], "#"));
    assert(!strcmp(s[0].output, "my file"));
    assert(!parse_pipeline("echo \"a\\qb\\\"c\\\\d\" 'x\\y' # ignored", s, &n));
    assert(!strcmp(s[0].command + s[0].offsets[1], "a\\qb\"c\\d"));
    assert(!strcmp(s[0].command + s[0].offsets[2], "x\\y"));
    assert(!parse_pipeline("cat <'input file' arg >>out", s, &n));
    assert(s[0].argc == 2 && s[0].append);
    const char *bad[] = {"echo '", "echo \\", "echo |", "| cat", "echo >", "echo > ''", "echo > a > b", "echo && cat", "echo & cat"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
        assert(parse_pipeline(bad[i], s, &n) < 0);
    assert(!setenv("AQUAOS_PARSER_TEST", "hello | world", 1));
    shell_last_status = 127;
    assert(!parse_pipeline("echo $AQUAOS_PARSER_TEST \"${AQUAOS_PARSER_TEST}\" '$AQUAOS_PARSER_TEST' \\$AQUAOS_PARSER_TEST $?", s, &n));
    assert(s[0].argc == 6);
    assert(!strcmp(s[0].command + s[0].offsets[1], "hello | world"));
    assert(!strcmp(s[0].command + s[0].offsets[2], "hello | world"));
    assert(!strcmp(s[0].command + s[0].offsets[3], "$AQUAOS_PARSER_TEST"));
    assert(!strcmp(s[0].command + s[0].offsets[4], "$AQUAOS_PARSER_TEST"));
    assert(!strcmp(s[0].command + s[0].offsets[5], "127"));
    char large[256]; memset(large, 'x', sizeof(large)-1); large[255] = 0;
    assert(!setenv("AQUAOS_PARSER_TEST", large, 1));
    assert(parse_pipeline("echo $AQUAOS_PARSER_TEST", s, &n) < 0);
    assert(parse_pipeline("echo ${BROKEN", s, &n) < 0);
    puts("shell parser checks passed");
}
