#ifndef _TEXTURE_LOADING
#define _TEXTURE_LOADING

#include "types.h"
#include <stdint.h>

#define TLD_ERR_MSG(result, message)                                           \
    {                                                                          \
        TldResult __res = result;                                              \
        if (__res != TLD_SUCCESS) {                                            \
            ERROR(message ": %s", tld_strerror(__res));                        \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    }
#define TLD_ERR(result) TLD_ERR_MSG(result, STR(result))

typedef enum {
    TLD_SUCCESS = 0,
    TLD_SYSTEM_ERROR,
    TLD_FAILED_TO_LOAD,
} TldResult;

TldResult tld_load_file(const char *filepath, Image *out_image);
void tld_free(Image *image);
const char *tld_strerror(TldResult result);

#endif
