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
    uint32_t vertex_count;
    uint32_t index_count;
    Vertex *vertices;
    uint32_t *indices;
} MeshData;

#endif
