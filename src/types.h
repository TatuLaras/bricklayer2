#ifndef _TYPES
#define _TYPES

#include "cglm/types-struct.h"
#include <stdint.h>

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

typedef struct {
    uint32_t vertex_count;
    uint32_t index_count;
    Vertex *vertices;
    uint32_t *indices;
} Mesh;

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
} Image;

#endif
