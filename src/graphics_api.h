#ifndef _GRAPHICS_API
#define _GRAPHICS_API

#include "gfx.h"
#include <vulkan/vulkan_core.h>

#define GFX_ERR_MSG(result, message)                                           \
    {                                                                          \
        GfxResult __res = result;                                              \
        if (__res != GFX_SUCCESS) {                                            \
            ERROR(message ": %s", gapi_strerror(__res));                       \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    }
#define GFX_ERR(result) GFX_ERR_MSG(result, STR(result))

typedef uint32_t GapiMeshHandle;
typedef uint32_t GapiObjectHandle;
typedef uint32_t GapiTextureHandle;

GfxResult gapi_init(GfxInitInfo *info);
VkResult gapi_get_vulkan_error(void);

GfxResult gapi_mesh_upload(Mesh *mesh, GapiMeshHandle *out_mesh_handle);
GfxResult gapi_texture_upload(uint32_t *pixels,
                              uint32_t width,
                              uint32_t height,
                              TextureHandle *out_texture_handle);

GfxResult gapi_object_create(GapiMeshHandle mesh_handle,
                             GapiTextureHandle texture_handle,
                             GapiObjectHandle *out_object_handle);
void gapi_object_set_matrix(GapiObjectHandle object_handle, mat4 *model_matrix);
GfxResult gapi_render_begin(void);
GfxResult gapi_render_end(void);

void gapi_object_draw(GapiObjectHandle object_handle);
int gapi_window_should_close(void);

const char *gapi_strerror(GfxResult result);

#endif
