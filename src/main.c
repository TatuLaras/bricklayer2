#include "graphics_api.h"
#include "model_loading.h"
#include "stb_image.h"

#include <stdlib.h>

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 400

const char shader_code[] = {
#embed "../build/shaders/shader.spv"
};

int main(void) {

    Mesh mesh;
    MLD_ERR(mld_init());
    MLD_ERR(mld_load_file("/home/tatu/_repos/ebb/assets/barrel.obj", &mesh));

    int width, height;
    char *path = "/home/tatu/_repos/ebb/assets/barrel.png";
    // char *path = "/home/tatu/test_obj/tex.png";
    uint8_t *pixels = stbi_load(path, &width, &height, NULL, STBI_rgb_alpha);

    if (pixels == NULL) {
        ERROR("failed to load image");
        exit(EXIT_FAILURE);
    }

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
    GFX_ERR(gapi_init(&init_info));

    GapiMeshHandle barrel = 0;
    GapiTextureHandle barrel_texture = 0;
    GFX_ERR(gapi_mesh_upload(&mesh, &barrel));
    GFX_ERR(gapi_texture_upload(
        (uint32_t *)pixels, width, height, &barrel_texture));

    GapiObjectHandle barrel_object = 0;
    GapiObjectHandle barrel_object2 = 0;
    GFX_ERR(gapi_object_create(barrel, barrel_texture, &barrel_object));
    GFX_ERR(gapi_object_create(barrel, barrel_texture, &barrel_object2));

    while (!gapi_window_should_close()) {
        GFX_ERR(gapi_render_begin());

        gapi_object_draw(barrel_object2);
        // gapi_object_draw(barrel_object);

        GFX_ERR(gapi_render_end());
    }

    mld_free();
    exit(EXIT_SUCCESS);
}
