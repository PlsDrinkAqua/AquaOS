#ifndef _ZENOS_TERMINAL_H
#define _ZENOS_TERMINAL_H 1

#ifdef __cplusplus
extern "C" {
#endif

int zenos_terminal_clear(void);
/* Shell foreground ownership and inherited command groups. */
int zenos_terminal_claim(void);
int zenos_terminal_begin_job(void);
int zenos_terminal_end_job(void);

#ifdef __cplusplus
}
#endif

#endif
