#include "gapi.h"
#include "gapi_types.h"
#include "grid.h"
#include "model_loading.h"
#include "orbital_controls.h"
#include "stb_image.h"
#include "user_input.h"

#include "user_input.h"
#include <stdlib.h>
#include <time.h>

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 400

#define WIDTH 12

const char shader_code[] = {
#embed "../build/shaders/shader.spv"
};

int main(void) {

    GapiInitInfo init_info = {
        .window = {.width = WINDOW_WIDTH,
                   .height = WINDOW_HEIGHT,
                   .title = "bricklayer2",
                   .flags = GAPI_WINDOW_RESIZEABLE},
        .shader_count = 1,
    };

    GLFWwindow *window;
    GAPI_ERR(gapi_init(&init_info, &window));
    uin_init(window);

    GapiPipelineHandle pipeline;
    GapiPipelineCreateInfo pipeline_create_info = {
        .shader_code = shader_code,
        .shader_code_size = sizeof shader_code,
        .alpha_blending_mode = GAPI_ALPHA_BLENDING_BLEND,
    };
    GAPI_ERR(gapi_pipeline_create(&pipeline_create_info, &pipeline));

    GapiPipelineHandle grid_pipeline;
    GapiObjectHandle grid_object;
    GAPI_ERR(grid_pipeline_create(&grid_pipeline));
    GAPI_ERR(grid_object_create(100, 1.0, &grid_object));

    MLD_ERR(mld_init());

    MldMesh mesh;
    int width, height;
    uint8_t *pixels;
    GapiMeshHandle barrel_mesh;
    GapiTextureHandle barrel_texture;
    GapiMeshHandle cube_mesh;
    GapiTextureHandle cube_texture;

    MLD_ERR(mld_load_file(
        "/home/tatu/_repos/ebb/assets/barrel.obj", &mesh, MLD_STORAGE_FAST));
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
    stbi_image_free(pixels);

    MLD_ERR(
        mld_load_file("/home/tatu/test_obj/cube.obj", &mesh, MLD_STORAGE_FAST));
    gapi_mesh_upload(&mesh, &cube_mesh);
    pixels = stbi_load(
        "/home/tatu/test_obj/tex.png", &width, &height, NULL, STBI_rgb_alpha);
    if (pixels == NULL) {
        ERROR("failed to load image");
        exit(EXIT_FAILURE);
    }
    GAPI_ERR(
        gapi_texture_upload((uint32_t *)pixels, width, height, &cube_texture));
    stbi_image_free(pixels);

    GapiObjectHandle barrels[WIDTH * WIDTH];
    GapiObjectHandle cubes[WIDTH * WIDTH];

    for (uint32_t i = 0; i < WIDTH * WIDTH; i++) {
        GAPI_ERR(gapi_object_create(barrel_mesh, barrel_texture, barrels + i));
        GAPI_ERR(gapi_object_create(cube_mesh, 0, cubes + i));
    }

    vec3 up = {0, 1, 0};
    GapiCamera camera = {
        .pos = {0, 5, 5},
        .target = {0, 0, 0},
        .up = {0, 1, 0},
        .fov_degrees = 45.0,
        .near_plane = 0.001,
        .far_plane = 10000.0,
    };

    // TODO: shouldnt have to create the rect
    GapiObjectHandle rect;
    GAPI_ERR(gapi_rect_create(barrel_texture, &rect));

    double mouse_x, mouse_y;
    double delta_time;

    while (!gapi_window_should_close(&delta_time)) {

        uin_get_mouse_delta(&mouse_x, &mouse_y);

        if (uin_is_mouse_button_down(UIN_MOUSE_BUTTON_MIDDLE)) {
            OrbitalMode mode = ORBITAL_NORMAL;
            if (uin_is_key_down(UIN_KEY_LEFT_SHIFT))
                mode = ORBITAL_SHIFT_POSITION;
            if (uin_is_key_down(UIN_KEY_LEFT_CONTROL))
                mode = ORBITAL_ZOOM;
            orbital_camera_update(&camera, mouse_x, mouse_y, mode);
        }
        if (uin_is_mouse_button_released(UIN_MOUSE_BUTTON_MIDDLE))
            uin_set_cursor(UIN_CURSOR_NORMAL);
        if (uin_is_mouse_button_pressed(UIN_MOUSE_BUTTON_MIDDLE))
            uin_set_cursor(UIN_CURSOR_DISABLED);

        float scroll_amount = uin_get_scroll();

        if (scroll_amount != 0.0) {
            orbital_camera_update_zoom(&camera, scroll_amount);
        }

        uint32_t window_width, window_height;
        gapi_get_window_size(&window_width, &window_height);

        struct timespec current_time = {0};
        ERR(clock_gettime(CLOCK_MONOTONIC, &current_time) < 0,
            "clock_gettime()");
        float current_time_seconds =
            current_time.tv_sec + current_time.tv_nsec / 1000000000.0;
        float rotation = 2 * M_PI * (fmod(current_time_seconds, 6.0) / 6.0);

        // camera.pos[2] = rotation;

        GAPI_ERR(gapi_render_begin(&camera));

        for (uint32_t x = 0; x < WIDTH; x++) {
            for (uint32_t y = 0; y < WIDTH; y++) {
                uint32_t i = y * WIDTH + x;
                mat4 matrix;
                glm_mat4_identity(matrix);
                glm_translate(matrix, (vec3){x, 0, y});
                glm_rotate(matrix, rotation * y * 0.2, up);

                gapi_object_draw(
                    barrels[i], pipeline, &matrix, GAPI_COLOR_WHITE);

                glm_translate(matrix, (vec3){0, 2, 0});
                glm_scale(matrix, (vec3){0.1, 0.1, 0.1});
                gapi_object_draw(cubes[i], pipeline, &matrix, GAPI_COLOR_WHITE);
            }
        }

        // gapi_rect_draw(rect,
        //                (Rect2D){.width = window_width / 3,
        //                         .height = window_height / 2,
        //                         .x = 40,
        //                         .y = 40},
        //                GAPI_COLOR_WHITE,
        //                pipeline);

        mat4 grid_matrix;
        glm_mat4_identity(grid_matrix);
        glm_translate(grid_matrix, (vec3){-50, 1, -50});
        gapi_object_draw(grid_object,
                         grid_pipeline,
                         &grid_matrix,
                         (vec4){1.0, 1.0, 1.0, 0.1});

        GAPI_ERR(gapi_render_end());
        uin_refresh();
    }

    mld_free();
    gapi_free();
    exit(EXIT_SUCCESS);
}
