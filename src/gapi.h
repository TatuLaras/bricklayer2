//  FIXME: Nothing is deinitialized
//  TODO: A mesh / texture data update function
#ifndef _GRAPHICS_API
#define _GRAPHICS_API

#include "cglm/cglm.h"
#include "cglm/types-struct.h"
#include "model_loading.h"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "gapi_types.h"
#include "log.h"

#define GAPI_ERR_MSG(result, message)                                          \
    {                                                                          \
        GapiResult __res = result;                                             \
        if (__res != GAPI_SUCCESS) {                                           \
            ERROR(message ": %s", gapi_strerror(__res));                       \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    }
#define GAPI_ERR(result) GAPI_ERR_MSG(result, STR(result))

#define SYS_ERR(result)                                                        \
    if ((result) < 0)                                                          \
        return GAPI_SYSTEM_ERROR;

#define STR(x) #x

#ifdef DEBUG
#define VK_ERR(result)                                                         \
    {                                                                          \
        VkResult __res = result;                                               \
        if (__res != VK_SUCCESS) {                                             \
            ERROR(STR(result) ": %s", string_VkResult(__res));                 \
            gapi_vulkan_error = __res;                                         \
            return GAPI_VULKAN_ERROR;                                          \
        }                                                                      \
    }
#else
#define VK_ERR(result)                                                         \
    {                                                                          \
        VkResult __res = result;                                               \
        if (__res != VK_SUCCESS) {                                             \
            vulkan_error = __res;                                              \
            return GAPI_VULKAN_ERROR;                                          \
        }                                                                      \
    }
#endif

#define PROPAGATE(result)                                                      \
    {                                                                          \
        GapiResult __res = result;                                             \
        if (__res != GAPI_SUCCESS) {                                           \
            return __res;                                                      \
        }                                                                      \
    }

#define SWAPCHAIN_MAX_IMAGES 16

#define GAPI_COLOR_WHITE (vec4){1.0, 1.0, 1.0, 1.0}
#define GAPI_COLOR_BLACK (vec4){0.0, 0.0, 0.0, 1.0}
#define GAPI_COLOR_RED (vec4){1.0, 0.0, 0.0, 1.0}
#define GAPI_COLOR_GREEN (vec4){0.0, 1.0, 0.0, 1.0}
#define GAPI_COLOR_BLUE (vec4){0.0, 0.0, 1.0, 1.0}

extern VkResult gapi_vulkan_error;

// Initialize the window and graphics context.
GapiResult gapi_init(GapiInitInfo *info, GLFWwindow **out_window);
void gapi_free(void);

// Upload mesh data to use for drawing. Opaque handle will be written to
// `out_mesh_handle`.
GapiResult gapi_mesh_upload(MldMesh *mesh, GapiMeshHandle *out_mesh_handle);
// Upload texture data to use for drawing. Opaque handle will be written to
// `out_texture_handle`.
GapiResult gapi_texture_upload(uint32_t *pixels,
                               uint32_t width,
                               uint32_t height,
                               GapiTextureHandle *out_texture_handle);

// Update existing mesh object `mesh_handle` to have mesh data in `mesh`.
GapiResult gapi_mesh_update(GapiMeshHandle mesh_handle, MldMesh *mesh);
// Update existing texture object `texture_handle` to have texture data in
// `pixels`.
GapiResult gapi_texture_update(GapiTextureHandle texture_handle,
                               uint32_t *pixels,
                               uint32_t width,
                               uint32_t height);

// Create a drawable 3D object from mesh and texture handles obtained from
// gapi_mesh_upload() and gapi_texture_upload() respectively. Opaque handle will
// be written to `out_object_handle`.
// An object can share mesh and texture data with other objects, although if
// you want to have a different model matrix / position them differently you
// have to have a separate object.
GapiResult gapi_object_create(GapiMeshHandle mesh_handle,
                              GapiTextureHandle texture_handle,
                              GapiObjectHandle *out_object_handle);
// Create an object with a 2D rectangle mesh. Drawable as normal with
// gapi_object_draw() but usually used with gapi_rect_draw().
GapiResult gapi_rect_create(GapiTextureHandle texture_handle,
                            GapiObjectHandle *out_object_handle);

GapiResult gapi_pipeline_create(GapiPipelineCreateInfo *create_info,
                                GapiPipelineHandle *out_pipeline_handle);

// Polls for GLFW events and returns whether or not the window should close.
// Returns frame delta-time into `out_delta_time`.
int gapi_window_should_close(double *out_delta_time);
// Call before any gapi*_draw functions. Optionally set `camera` for 3D
// rendering.
GapiResult gapi_render_begin(GapiCamera *camera);
// Call after any gapi*_draw functions.
GapiResult gapi_render_end(void);

// Draw a 3D object (`object_handle`) created with gapi_object_create() using
// model matrix `matrix`.
void gapi_object_draw(GapiObjectHandle object_handle,
                      GapiPipelineHandle shader_handle,
                      mat4 *matrix,
                      vec4 color_tint);
// Draw a rectangle object `object_handle` on screen flatly without perspective
// or view transforms.
void gapi_rect_draw(GapiObjectHandle object_handle,
                    Rect2D rect,
                    vec4 color,
                    GapiPipelineHandle shader_handle);

void gapi_get_window_size(uint32_t *out_width, uint32_t *out_height);

// Get the result of the last failed Vulkan API call.
VkResult gapi_get_vulkan_error(void);
// Returns string representation of a GapiResult error code `result`.
const char *gapi_strerror(GapiResult result);

#endif
