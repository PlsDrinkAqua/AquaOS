/* assert may be included again after changing NDEBUG. */
#undef assert
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
void aquaos_assert_fail(const char *expression, const char *file, int line)
    __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#define assert(expression) ((expression) ? (void)0 : aquaos_assert_fail(#expression, __FILE__, __LINE__))
#endif
