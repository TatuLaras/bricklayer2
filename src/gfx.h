/*
 * Graphics, Vulkan wrapper
 */

#ifndef _GFX
#define _GFX

#include <stdint.h>

typedef enum {
    GFX_WINDOW_RESIZEABLE = 1,
} GfxWindowFlags;

int gfx_init_window(uint32_t width,
                    uint32_t height,
                    const char *title,
                    GfxWindowFlags flags);

#endif
