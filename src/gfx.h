#ifndef _GFX
#define _GFX

#include "cglm/types-struct.h"
#include <stdint.h>

typedef enum {
    GFX_SUCCESS = 0,
} GfxResult;

typedef uint32_t MeshHandle;
typedef uint32_t TextureHandle;

typedef enum {
    GFX_WINDOW_RESIZEABLE = 1,
} GfxWindowFlags;

typedef struct {
    uint32_t width;
    uint32_t height;
    const char *title;
    GfxWindowFlags flags;
} GfxWindowInitInfo;

typedef struct {
    const char *code;
    uint32_t size;
} GfxShader;

typedef struct {
    GfxWindowInitInfo window;
    uint32_t shader_count;
    GfxShader *shaders;
} GfxInitInfo;

typedef struct {
    vec3s pos;
    vec3s target;
    vec3s up;
    float fov_degrees;
} Camera;

typedef struct {
    vec3s pos;
    vec3s color;
    vec3s normal;
    vec2s uv;
} Vertex;

typedef struct {
    uint32_t vertex_count;
    uint32_t index_count;
    Vertex *vertices;
    uint32_t *indices;
} Mesh;

typedef struct {
    MeshHandle mesh;
    TextureHandle texture;
    uint32_t shader_index;
} Model;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} Rect;

void gfx_init(GfxInitInfo *info);

int gfx_camera_set(Camera *camera);
int gfx_render_begin(void);
int gfx_render_end(void);
int gfx_texture_render_begin(void);
int gfx_texture_render_end(void);

int gfx_texture_upload(uint32_t *pixels,
                       uint32_t width,
                       uint32_t height,
                       TextureHandle *out_handle);
int gfx_mesh_upload(Mesh mesh, MeshHandle *out_handle);

int gfx_model_draw(Model model);
int gfx_rect_draw(Rect rect);

#endif
