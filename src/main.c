#include "cglm/cglm.h"
#include "graphics_api.h"
#include "model_loading.h"
#include "stb_image.h"

#include <stdlib.h>
#include <time.h>

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 400

const char shader_code[] = {
#embed "../build/shaders/shader.spv"
};

int main(void) {

    MeshData mesh;
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

    GapiShader shader = {
        .code = shader_code,
        .size = sizeof shader_code,
    };

    GapiInitInfo init_info = {
        .window = {.width = WINDOW_WIDTH,
                   .height = WINDOW_HEIGHT,
                   .title = "My cool window",
                   .flags = GAPI_WINDOW_RESIZEABLE},
        .shader_count = 1,
        .shaders = &shader,
    };
    GAPI_ERR(gapi_init(&init_info));

    GapiMeshHandle barrel = 0;
    GapiTextureHandle barrel_texture = 0;
    GAPI_ERR(gapi_mesh_upload(&mesh, &barrel));
    GAPI_ERR(gapi_texture_upload(
        (uint32_t *)pixels, width, height, &barrel_texture));

#define WIDTH 12

    GapiObjectHandle barrels[WIDTH * WIDTH] = {0};
    for (uint32_t x = 0; x < WIDTH; x++) {
        for (uint32_t y = 0; y < WIDTH; y++) {
            uint32_t i = y * WIDTH + x;
            GAPI_ERR(gapi_object_create(barrel, barrel_texture, barrels + i));
        }
    }

    vec3 up = {0, 1, 0};
    vec3 right = {1, 0, 0};
    GapiCamera camera = {
        .pos = {0, 6, 6},
        .target = {6, 0, 6},
        .up = {0, 1, 0},
        .fov_degrees = 45.0,
    };

    while (!gapi_window_should_close()) {

        struct timespec current_time = {0};
        ERR(clock_gettime(CLOCK_MONOTONIC, &current_time) < 0,
            "clock_gettime()");
        float current_time_seconds =
            current_time.tv_sec + current_time.tv_nsec / 1000000000.0;
        float rotation = 2 * M_PI * (fmod(current_time_seconds, 6.0) / 6.0);

        camera.pos[2] = rotation;

        GAPI_ERR(gapi_render_begin(&camera));

        for (uint32_t x = 0; x < WIDTH; x++) {
            for (uint32_t y = 0; y < WIDTH; y++) {
                uint32_t i = y * WIDTH + x;

                mat4 matrix;
                glm_mat4_identity(matrix);
                glm_translate(matrix, (vec3){x, 0, y});
                glm_rotate(matrix, rotation * y * 0.2, up);
                // glm_rotate(matrix, rotation, right);

                gapi_object_draw(barrels[i], &matrix);
            }
        }

        GAPI_ERR(gapi_render_end());
    }

    mld_free();
    exit(EXIT_SUCCESS);
}
