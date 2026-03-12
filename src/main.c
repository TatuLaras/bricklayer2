#include "cglm/cglm.h"
#include "firewatch.h"
#include "font_loading.h"
#include "gapi.h"
#include "gapi_types.h"
#include "grid.h"
#include "model_loading.h"
#include "orbital_controls.h"
#include "texture_loading.h"
#include "user_input.h"
#include "utility_macros.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 400
#define MAX_FRAMERATE 300
#define TEXTURE_BINDING 1
#define UBO_BINDING 0

#define GRID_SIZE 20

const char shader_code[] = {
#embed "../build/shaders/shader.spv"
};

const GapiCamera initial_camera = {
    .pos = {0.01, 2, 3},
    .target = {0, 0, 0},
    .up = {0, 1, 0},
    .fov_degrees = 45.0,
    .near_plane = 0.001,
    .far_plane = 10000.0,
};

static struct {
    int is_grid_disabled;
    int is_debug_info_enabled;
} opts = {0};

static char buf[4096] = "Hello";
static uint32_t buf_i = 5;

void texture_callback(const char *filepath, uint64_t cookie) {

    Image image;
    TLD_ERR(tld_load_file(filepath, &image));
    GAPI_ERR(gapi_texture_update((GapiTextureHandle)cookie, &image));
    tld_free(&image);
}

void mesh_callback(const char *filepath, uint64_t cookie) {

    MldMesh mesh;
    MldResult mld_res = mld_load_file(filepath, &mesh, MLD_STORAGE_MALLOC);
    if (mld_res != MLD_SUCCESS) {
        ERROR("Could not load mesh '%s': %s", filepath, mld_strerror(mld_res));
        return;
    }
    GAPI_ERR(gapi_mesh_update((GapiMeshHandle)cookie, &mesh.mesh));
    free(mesh.mesh.vertices);
    free(mesh.mesh.indices);
}

void character_callback(GLFWwindow *window, unsigned int codepoint) {
    if (buf_i >= sizeof buf)
        buf_i = 0;
    buf[buf_i++] = codepoint;
}

void usage_exit(char *program_name) {

    ERROR("usage: %s MODEL_FILE TEXTURE_FILE ...", program_name);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {

    if (argc < 2 || argc % 2 != 1)
        usage_exit(*argv);

    uint32_t model_count = (argc - 1) / 2;
    uint32_t model_i = 0;
    uint32_t texture_i = 0;
    char *models[model_count];
    char *textures[model_count];

    for (int i = 1; i < argc; i++) {
        if (i % 2 == 1)
            models[model_i++] = argv[i];
        else
            textures[texture_i++] = argv[i];
    }

    GLFWwindow *window;
    GapiInitInfo init_info = {
        .window = {.width = WINDOW_WIDTH,
                   .height = WINDOW_HEIGHT,
                   .title = "bricklayer2",
                   .flags = GAPI_WINDOW_RESIZEABLE},
    };
    GAPI_ERR(gapi_init(&init_info, &window));
    uin_init(window);
    glfwSetCharCallback(window, character_callback);

    GapiPipelineHandle pipeline;
    GapiDescriptorLayoutItem layout_items[] = {
        {
            .binding = UBO_BINDING,
            .type = GAPI_DESCRIPTOR_UNIFORM_BUFFER,
            .stage = GAPI_STAGE_VERTEX,
        },
        {
            .binding = TEXTURE_BINDING,
            .type = GAPI_DESCRIPTOR_TEXTURE,
            .stage = GAPI_STAGE_FRAGMENT,
        },
    };
    GapiPipelineCreateInfo pipeline_create_info = {
        .shader_code = shader_code,
        .shader_code_size = sizeof shader_code,
        .alpha_blending_mode = GAPI_ALPHA_BLENDING_BLEND,
        .layout_item_count = COUNT(layout_items),
        .layout_items = layout_items,
    };
    GAPI_ERR(gapi_pipeline_create(&pipeline_create_info, &pipeline));

    GapiPipelineHandle grid_pipeline;
    GapiObjectHandle grid_object;
    GAPI_ERR(grid_pipeline_create(&grid_pipeline));
    GAPI_ERR(grid_object_create(GRID_SIZE, 1.0, &grid_object));

    MLD_ERR(mld_init());

    // Load font
    GapiFontHandle font;
    Font font_data;
    FLD_ERR(fld_load_file(
        "/usr/share/fonts/OTF/MonaspaceXenon-SemiBold.otf", 0, 13, &font_data));
    GAPI_ERR(gapi_font_upload(&font_data, &font));

    GapiObjectHandle objects[model_count];
    for (uint32_t i = 0; i < model_count; i++) {

        GapiMeshHandle mesh_handle;
        GAPI_ERR(gapi_mesh_reserve(&mesh_handle));
        firewatch_new_file(models[i], mesh_handle, mesh_callback, 0);

        GapiTextureHandle texture_handle;
        GAPI_ERR(gapi_texture_reserve(TEXTURE_BINDING, &texture_handle));
        firewatch_new_file(textures[i], texture_handle, texture_callback, 0);

        GAPI_ERR(gapi_object_create(
            mesh_handle, texture_handle, UBO_BINDING, objects + i));
    }

    GapiCamera camera = initial_camera;

    double mouse_x, mouse_y;
    double delta_time = 0;
    char fps_string[32] = {0};
    double fps = 0;

    while (!gapi_window_should_close(MAX_FRAMERATE, &delta_time)) {

        firewatch_check();

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

        // Reset camera
        if (uin_is_key_pressed(UIN_KEY_C) &&
            uin_is_key_down(UIN_KEY_LEFT_SHIFT)) {
            camera = initial_camera;
        }

        if (uin_is_key_pressed(UIN_KEY_G))
            opts.is_grid_disabled = !opts.is_grid_disabled;

        if (uin_is_key_pressed(UIN_KEY_F1))
            opts.is_debug_info_enabled = !opts.is_debug_info_enabled;

        GAPI_ERR(gapi_render_begin(&camera));
        Color clear = (Color){0x21, 0x0d, 0x1f, 0xff};
        gapi_clear(&clear);

        mat4 matrix;
        glm_mat4_identity(matrix);
        for (uint32_t i = 0; i < model_count; i++) {
            gapi_object_draw(objects[i], pipeline, &matrix, GAPI_COLOR_WHITE);
        }

        if (!opts.is_grid_disabled) {
            mat4 grid_matrix;
            glm_mat4_identity(grid_matrix);
            glm_translate(
                grid_matrix,
                (vec3){-(int)(GRID_SIZE / 2), 0, -(int)(GRID_SIZE / 2)});
            gapi_object_draw(grid_object,
                             grid_pipeline,
                             &grid_matrix,
                             (vec4){1.0, 1.0, 1.0, 0.1});
        }

        gapi_clear(NULL);
        if (opts.is_debug_info_enabled) {

            fps = fps * (10 - 1) / 10 + (1.0 / delta_time) / 10;
            snprintf(fps_string, sizeof fps_string, "FPS: %u", (uint32_t)fps);
            gapi_text_draw(fps_string, 10, 0, font, GAPI_COLOR_WHITE);
        }

        GAPI_ERR(gapi_render_end());
        uin_refresh();
    }

    mld_free();
    gapi_free();
    exit(EXIT_SUCCESS);
}
