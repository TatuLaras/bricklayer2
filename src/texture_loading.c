#include "texture_loading.h"
#include "cute_aseprite.h"
#include "stb_image.h"
#include <string.h>

static inline TldResult load_aseprite_file(const char *filepath,
                                           Image *out_image) {

    ase_t *aseprite_file = cute_aseprite_load_from_file(filepath, 0);
    if (!aseprite_file || aseprite_file->frame_count == 0)
        return TLD_FAILED_TO_LOAD;

    ase_frame_t *frame = &aseprite_file->frames[0];

    size_t size = aseprite_file->w * aseprite_file->h * sizeof(uint32_t);
    Image image = {
        .pixels = malloc(size),
        .width = aseprite_file->w,
        .height = aseprite_file->h,
    };
    if (!image.pixels)
        return TLD_SYSTEM_ERROR;
    memcpy(image.pixels, frame->pixels, size);

    cute_aseprite_free(aseprite_file);

    *out_image = image;
    return TLD_SUCCESS;
}

TldResult tld_load_file(const char *filepath, Image *out_image) {

    char *extension = strrchr(filepath, '.');

    if (!strcmp(extension, ".aseprite")) {
        return load_aseprite_file(filepath, out_image);
    }

    int width, height;
    uint8_t *pixels = stbi_load(filepath, &width, &height, NULL, 4);
    if (!pixels)
        return TLD_FAILED_TO_LOAD;

    out_image->pixels = (uint32_t *)pixels;
    out_image->width = width;
    out_image->height = height;
    return TLD_SUCCESS;
}

void tld_free(Image *image) {

    if (image->pixels != NULL) {
        free(image->pixels);
        image->pixels = NULL;
    }
}

const char *tld_strerror(TldResult result) {

    switch (result) {

    case TLD_SUCCESS:
        return "Success";
    case TLD_SYSTEM_ERROR:
        return "A system error occurred (likely with malloc()), check errno";
    case TLD_FAILED_TO_LOAD:
        return "Failed to load image, memory allocation error or "
               "mangled/unsupported image file";
    }

    return "";
}
