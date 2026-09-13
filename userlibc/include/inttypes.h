#ifndef _USERLIBC_INTTYPES_H
#define _USERLIBC_INTTYPES_H
#include <stdint.h>
#define PRId8 "hhd"
#define SCNd8 "hhd"
#define PRIi8 "hhi"
#define SCNi8 "hhi"
#define PRIo8 "hho"
#define SCNo8 "hho"
#define PRIu8 "hhu"
#define SCNu8 "hhu"
#define PRIx8 "hhx"
#define SCNx8 "hhx"
#define PRIX8 "hhX"
#define SCNX8 "hhX"
#define PRId16 "hd"
#define SCNd16 "hd"
#define PRIi16 "hi"
#define SCNi16 "hi"
#define PRIo16 "ho"
#define SCNo16 "ho"
#define PRIu16 "hu"
#define SCNu16 "hu"
#define PRIx16 "hx"
#define SCNx16 "hx"
#define PRIX16 "hX"
#define SCNX16 "hX"
#define PRId32 "d"
#define SCNd32 "d"
#define PRIi32 "i"
#define SCNi32 "i"
#define PRIo32 "o"
#define SCNo32 "o"
#define PRIu32 "u"
#define SCNu32 "u"
#define PRIx32 "x"
#define SCNx32 "x"
#define PRIX32 "X"
#define SCNX32 "X"
#ifdef __LP64__
#define PRId64 "ld"
#define SCNd64 "ld"
#else
#define PRId64 "lld"
#define SCNd64 "lld"
#endif
#ifdef __LP64__
#define PRIi64 "li"
#define SCNi64 "li"
#else
#define PRIi64 "lli"
#define SCNi64 "lli"
#endif
#ifdef __LP64__
#define PRIo64 "lo"
#define SCNo64 "lo"
#else
#define PRIo64 "llo"
#define SCNo64 "llo"
#endif
#ifdef __LP64__
#define PRIu64 "lu"
#define SCNu64 "lu"
#else
#define PRIu64 "llu"
#define SCNu64 "llu"
#endif
#ifdef __LP64__
#define PRIx64 "lx"
#define SCNx64 "lx"
#else
#define PRIx64 "llx"
#define SCNx64 "llx"
#endif
#ifdef __LP64__
#define PRIX64 "lX"
#define SCNX64 "lX"
#else
#define PRIX64 "llX"
#define SCNX64 "llX"
#endif
#endif
