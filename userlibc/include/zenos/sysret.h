#ifndef _ZENOS_SYSRET_H
#define _ZENOS_SYSRET_H

#include <errno.h>

static inline int zenos_sysret(int result) {
    if (result < 0) {
        errno = -result;
        return -1;
    }
    return result;
}

#endif
