#ifndef _GAPI_TYPES
#define _GAPI_TYPES

#include "cglm/types-struct.h"
#include "cglm/types.h"
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#define GAPI_MAX_FRAMES_IN_FLIGHT 2
#define GAPI_MAX_LAYOUT_BINDINGS 32

typedef enum {
    GAPI_SUCCESS = 0,
    GAPI_ERROR_GENERIC,
    GAPI_SYSTEM_ERROR,
    GAPI_VULKAN_ERROR,
    GAPI_GLFW_ERROR,
    GAPI_NO_DEVICE_FOUND,
    GAPI_VULKAN_FEATURE_UNSUPPORTED,
    GAPI_INVALID_HANDLE,
    GAPI_TOO_MANY_LAYOUT_BINDINGS,
} GapiResult;

typedef enum {
    GAPI_WINDOW_RESIZEABLE = 1,
} GapiWindowFlags;

typedef enum {
    GAPI_ALPHA_BLENDING_NONE = 0,
    GAPI_ALPHA_BLENDING_BLEND,
    GAPI_ALPHA_BLENDING_ADDITIVE,
} GapiAlphaBlending;

typedef enum {
    GAPI_TOPOLOGY_TRIANGLES = 0,
    GAPI_TOPOLOGY_LINES,
    GAPI_TOPOLOGY_POINTS,
} GapiTopology;

typedef uint32_t GapiMeshHandle;
typedef uint32_t GapiObjectHandle;
typedef uint32_t GapiTextureHandle;
typedef uint32_t GapiPipelineHandle;

typedef struct {
    uint32_t width;
    uint32_t height;
    const char *title;
    GapiWindowFlags flags;
} GapiWindowInitInfo;

typedef struct {
    const char *shader_code;
    uint32_t shader_code_size;
    GapiAlphaBlending alpha_blending_mode;
    GapiTopology topology;
} GapiPipelineCreateInfo;

typedef struct {
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
} GapiPipeline;

typedef struct {
    GapiWindowInitInfo window;
    uint32_t shader_count;
} GapiInitInfo;

typedef struct {
    vec3 pos;
    vec3 target;
    vec3 up;
    float fov_degrees;
    float near_plane;
    float far_plane;
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
    vec4 color_tint;
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
    VkBuffer uniform_buffers[GAPI_MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory uniform_buffer_memories[GAPI_MAX_FRAMES_IN_FLIGHT];
    void *uniform_buffer_mappings[GAPI_MAX_FRAMES_IN_FLIGHT];
} GapiObject;

typedef struct {
    vec3s pos;
    vec3s color;
    vec3s normal;
    vec2s uv;
} Vertex;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} Rect2D;

#endif
