#ifndef KERNEL_USERCOPY_H
#define KERNEL_USERCOPY_H

#include <stddef.h>

int copy_from_user(void *dest, const void *user_src, size_t size);
int copy_to_user(void *user_dest, const void *src, size_t size);
int copy_string_from_user(char *dest, size_t dest_size, const char *user_src);

#endif
