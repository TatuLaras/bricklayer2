#ifndef _TEXTURE_LOADING
#define _TEXTURE_LOADING

#include <stdint.h>

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
} TldImage;

#endif
