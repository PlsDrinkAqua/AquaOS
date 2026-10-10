#pragma once
#include <stdint.h>
typedef struct __attribute__((aligned(4096))) { uint64_t entries[512]; } page_directory_t;
