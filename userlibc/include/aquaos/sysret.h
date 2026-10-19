#ifndef _AQUAOS_SYSRET_H
#define _AQUAOS_SYSRET_H

#include <errno.h>

static inline int aquaos_sysret(int result) {
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

#endif
