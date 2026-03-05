#ifndef _GRAPHICS_API
#define _GRAPHICS_API

#include "cglm/cglm.h"
#include "cglm/types-struct.h"
#include <vulkan/vulkan_core.h>

#include "model_loading.h"

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

typedef enum {
    GAPI_SUCCESS = 0,
    GAPI_ERROR_GENERIC,
    GAPI_SYSTEM_ERROR,
    GAPI_VULKAN_ERROR,
    GAPI_GLFW_ERROR,
    GAPI_NO_DEVICE_FOUND,
    GAPI_VULKAN_FEATURE_UNSUPPORTED,
} GapiResult;

typedef enum {
    GAPI_WINDOW_RESIZEABLE = 1,
} GapiWindowFlags;

typedef struct {
    uint32_t width;
    uint32_t height;
    const char *title;
    GapiWindowFlags flags;
} GapiWindowInitInfo;

typedef struct {
    const char *code;
    uint32_t size;
} GapiShader;

typedef struct {
    GapiWindowInitInfo window;
    uint32_t shader_count;
    GapiShader *shaders;
} GapiInitInfo;

typedef struct {
    vec3 pos;
    vec3 target;
    vec3 up;
    float fov_degrees;
} GapiCamera;

typedef uint32_t GapiMeshHandle;
typedef uint32_t GapiObjectHandle;
typedef uint32_t GapiTextureHandle;

extern VkResult gapi_vulkan_error;

// Initialize the window and graphics context.
GapiResult gapi_init(GapiInitInfo *info);

// Upload mesh data to use for drawing. Opaque handle will be written to
// `out_mesh_handle`.
GapiResult gapi_mesh_upload(MeshData *mesh, GapiMeshHandle *out_mesh_handle);
// Upload texture data to use for drawing. Opaque handle will be written to
// `out_texture_handle`.
GapiResult gapi_texture_upload(uint32_t *pixels,
                               uint32_t width,
                               uint32_t height,
                               GapiTextureHandle *out_texture_handle);
// Create a drawable object from mesh and texture handles obtained from
// gapi_mesh_upload() and gapi_texture_upload() respectively. Opaque handle will
// be written to `out_object_handle`.
GapiResult gapi_object_create(GapiMeshHandle mesh_handle,
                              GapiTextureHandle texture_handle,
                              GapiObjectHandle *out_object_handle);

// Polls for GLFW events and returns whether or not the window should close.
int gapi_window_should_close(void);
// Call before any gapi*_draw functions. Optionally set `camera` for 3D
// rendering.
GapiResult gapi_render_begin(GapiCamera *camera);
// Call after any gapi*_draw functions.
GapiResult gapi_render_end(void);

// Draw a 3D object (`object_handle`) created with gapi_object_create() using
// model matrix `matrix`.
void gapi_object_draw(GapiObjectHandle object_handle, mat4 *matrix);

// Get the result of the last failed Vulkan API call.
VkResult gapi_get_vulkan_error(void);
// Returns string representation of a GapiResult error code `result`.
const char *gapi_strerror(GapiResult result);

#endif
