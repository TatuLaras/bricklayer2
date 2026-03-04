#include "gfx.h"
#include <stdlib.h>

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 400

const char shader_code[] = {
#embed "../build/shaders/shader.spv"
};

int main(void) {
    GfxShader shader = {
        .code = shader_code,
        .size = sizeof shader_code,
    };

    GfxInitInfo init_info = {
        .window = {.width = WINDOW_WIDTH,
                   .height = WINDOW_HEIGHT,
                   .title = "My cool window",
                   .flags = GFX_WINDOW_RESIZEABLE},
        .shader_count = 1,
        .shaders = &shader,
    };
    gfx_init(&init_info);

    while (1)
        ;

    exit(EXIT_SUCCESS);
}
