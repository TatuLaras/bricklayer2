#define CUTE_ASEPRITE_IMPLEMENTATION

#include "aseprite_texture.h"

// Ignore some warnings from external library
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#include "cute_aseprite.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <raylib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline Image aseprite_load_unlit_data(ase_t *aseprite_file) {
    ase_frame_t *frame = &aseprite_file->frames[0];

    const char *unlit_layer_name = "_unlit";

    for (int i = 0; i < frame->cel_count; i++) {
        ase_layer_t *layer = frame->cels[i].layer;
        if (!strcmp(layer->name, unlit_layer_name)) {
            ase_cel_t *cel = &frame->cels[i];

            Image unlit_data = {
                .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                .mipmaps = 1,
                .width = cel->w,
                .height = cel->h,
            };

            size_t data_size = cel->w * cel->h * sizeof(ase_color_t);
            unlit_data.data = malloc(data_size);
            memcpy(unlit_data.data, frame->cels[i].pixels, data_size);

            return unlit_data;
        }
    }
    return (Image){0};
}

ImageData aseprite_load(const char *filepath) {
    ase_t *aseprite_file = cute_aseprite_load_from_file(filepath, 0);
    if (!aseprite_file || aseprite_file->frame_count == 0)
        return (ImageData){0};

    ase_frame_t *frame = &aseprite_file->frames[0];

    Image base_image = {
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        .mipmaps = 1,
        .height = aseprite_file->h,
        .width = aseprite_file->w,
    };

    size_t data_size =
        base_image.width * base_image.height * sizeof(ase_color_t);
    uint32_t *pixel_data = malloc(data_size);
    memcpy(pixel_data, frame->pixels, data_size);
    base_image.data = pixel_data;

    Image unlit_data = aseprite_load_unlit_data(aseprite_file);
    cute_aseprite_free(aseprite_file);

    return (ImageData){.base_image = base_image, .unlit_data = unlit_data};
}

void aseprite_image_data_free(ImageData *image_data) {
    if (image_data->base_image.data)
        UnloadImage(image_data->base_image);

    if (image_data->unlit_data.data)
        UnloadImage(image_data->unlit_data);
}
