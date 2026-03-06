#ifndef _GAPI_TYPES
#define _GAPI_TYPES

#include "cglm/types.h"
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#define GAPI_MAX_FRAMES_IN_FLIGHT 2

typedef enum {
    GAPI_SUCCESS = 0,
    GAPI_ERROR_GENERIC,
    GAPI_SYSTEM_ERROR,
    GAPI_VULKAN_ERROR,
    GAPI_GLFW_ERROR,
    GAPI_NO_DEVICE_FOUND,
    GAPI_VULKAN_FEATURE_UNSUPPORTED,
    GAPI_INVALID_HANDLE,
} GapiResult;

typedef enum {
    GAPI_WINDOW_RESIZEABLE = 1,
} GapiWindowFlags;

typedef uint32_t GapiMeshHandle;
typedef uint32_t GapiObjectHandle;
typedef uint32_t GapiTextureHandle;

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

typedef struct {
    VkImage image;
    VkDeviceMemory image_memory;
    VkImageView image_view;
    VkSampler sampler;
} GapiTexture;

typedef struct {
    mat4 model;
    mat4 view;
    mat4 projection;
} GapiUBO;

typedef struct {
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    VkDeviceMemory vertex_memory;
    VkDeviceMemory index_memory;
    uint32_t index_count;
} GapiMesh;

typedef struct {
    GapiMeshHandle mesh_handle;
    GapiTextureHandle texture_handle;
    vec4 model_matrix[4];
    VkBuffer uniform_buffers[GAPI_MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory uniform_buffer_memories[GAPI_MAX_FRAMES_IN_FLIGHT];
    void *uniform_buffer_mappings[GAPI_MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSet descriptor_sets[GAPI_MAX_FRAMES_IN_FLIGHT];
} GapiObject;

#endif
