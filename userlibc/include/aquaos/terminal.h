#ifndef _AQUAOS_TERMINAL_H
#define _AQUAOS_TERMINAL_H 1

#ifdef __cplusplus
extern "C" {
#endif

int aquaos_terminal_clear(void);
/* Shell foreground ownership and inherited command groups. */
int aquaos_terminal_claim(void);
int aquaos_terminal_begin_job(void);
int aquaos_terminal_end_job(void);

#ifdef __cplusplus
}
#endif

#endif
