#include <stddef.h>
#include <stdint.h>

#include <libk/string.h>

#include <kernel/usercopy.h>
#include <kernel/vmm.h>

int copy_from_user(void *dest, const void *user_src, size_t size) {
    if ((!dest && size != 0) ||
        !vmm_user_range_valid(user_src, size, false)) {
        return -1;
    }

    kmemcpy(dest, user_src, size);
    return 0;
}

int copy_to_user(void *user_dest, const void *src, size_t size) {
    if ((!src && size != 0) ||
        !vmm_user_range_valid(user_dest, size, true)) {
        return -1;
    }

    kmemcpy(user_dest, src, size);
    return 0;
}

int copy_string_from_user(char *dest, size_t dest_size, const char *user_src) {
    size_t i;

    if (!dest || dest_size == 0 || !user_src) {
        return -1;
    }

    for (i = 0; i + 1 < dest_size; i++) {
        if (!vmm_user_range_valid(user_src + i, 1, false)) {
            dest[0] = '\0';
            return -1;
        }

        dest[i] = user_src[i];
        if (dest[i] == '\0') {
            return 0;
        }
    }

    dest[dest_size - 1] = '\0';
    return -1;
}
