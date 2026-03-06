#include "cglm/cglm.h"
#include "gapi.h"
#include "gapi_types.h"
#include "model_loading.h"
#include "stb_image.h"
#include "utility_macros.h"

#include <stdlib.h>
#include <time.h>

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 400

#define WIDTH 12

const char shader_code[] = {
#embed "../build/shaders/shader.spv"
};
const char shader_2_code[] = {
#embed "../build/shaders/shader2.spv"
};

int main(void) {

    GapiInitInfo init_info = {
        .window = {.width = WINDOW_WIDTH,
                   .height = WINDOW_HEIGHT,
                   .title = "My cool window",
                   .flags = GAPI_WINDOW_RESIZEABLE},
        .shader_count = 1,
    };

    GAPI_ERR(gapi_init(&init_info));

    GapiPipelineHandle shader;
    GapiPipelineHandle shader2;
    GapiPipelineCreateInfo shader_create_info = {
        .shader_code = shader_code,
        .shader_code_size = sizeof shader_code,
        .alpha_blending_mode = GAPI_ALPHA_BLENDING_BLEND,
    };
    GAPI_ERR(gapi_shader_create(&shader_create_info, &shader));

    shader_create_info.shader_code = shader_2_code;
    shader_create_info.shader_code_size = sizeof shader_2_code;
    GAPI_ERR(gapi_shader_create(&shader_create_info, &shader2));

    MLD_ERR(mld_init());

    MeshData mesh;
    int width, height;
    uint8_t *pixels;
    GapiMeshHandle barrel_mesh;
    GapiTextureHandle barrel_texture;
    GapiMeshHandle cube_mesh;
    GapiTextureHandle cube_texture;

    MLD_ERR(mld_load_file("/home/tatu/_repos/ebb/assets/barrel.obj", &mesh));
    gapi_mesh_upload(&mesh, &barrel_mesh);
    pixels = stbi_load("/home/tatu/_repos/ebb/assets/barrel.png",
                       &width,
                       &height,
                       NULL,
                       STBI_rgb_alpha);
    if (pixels == NULL) {
        ERROR("failed to load image");
        exit(EXIT_FAILURE);
    }
    GAPI_ERR(gapi_texture_upload(
        (uint32_t *)pixels, width, height, &barrel_texture));

    MLD_ERR(mld_load_file("/home/tatu/test_obj/cube.obj", &mesh));
    gapi_mesh_upload(&mesh, &cube_mesh);
    pixels = stbi_load(
        "/home/tatu/test_obj/tex.png", &width, &height, NULL, STBI_rgb_alpha);
    if (pixels == NULL) {
        ERROR("failed to load image");
        exit(EXIT_FAILURE);
    }
    GAPI_ERR(
        gapi_texture_upload((uint32_t *)pixels, width, height, &cube_texture));

    GapiObjectHandle barrels[WIDTH * WIDTH];
    GapiObjectHandle cubes[WIDTH * WIDTH];

    for (uint32_t i = 0; i < WIDTH * WIDTH; i++) {
        GAPI_ERR(gapi_object_create(barrel_mesh, barrel_texture, barrels + i));
        GAPI_ERR(gapi_object_create(cube_mesh, 0, cubes + i));
    }

    vec3 up = {0, 1, 0};
    vec3 right = {1, 0, 0};
    GapiCamera camera = {
        .pos = {0, 6, 6},
        .target = {6, 0, 6},
        .up = {0, 1, 0},
        .fov_degrees = 45.0,
    };

    // TODO: shouldnt have to create the rect
    GapiObjectHandle rect;
    GAPI_ERR(gapi_rect_create(0, &rect));

    while (!gapi_window_should_close()) {

        uint32_t window_width, window_height;
        gapi_get_window_size(&window_width, &window_height);

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
                // GapiPipelineHandle shader_to_use =
                //     (i % 2 == 0) ? shader : shader2;
                GapiPipelineHandle shader_to_use = shader;

                mat4 matrix;
                glm_mat4_identity(matrix);
                glm_translate(matrix, (vec3){x, 0, y});
                glm_rotate(matrix, rotation * y * 0.2, up);
                // glm_rotate(matrix, rotation, right);

                gapi_object_draw(barrels[i], shader_to_use, &matrix);

                glm_translate(matrix, (vec3){0, 2, 0});
                glm_scale(matrix, (vec3){0.1, 0.1, 0.1});
                gapi_object_draw(cubes[i], shader_to_use, &matrix);
            }
        }

        gapi_rect_draw(rect,
                       (Rect2D){.width = window_width / 3,
                                .height = window_height / 2,
                                .x = 40,
                                .y = 40},
                       (vec4){1.0, 0.0, 0.1, 0.5},
                       shader2);

        GAPI_ERR(gapi_render_end());
    }

    mld_free();
    exit(EXIT_SUCCESS);
}
