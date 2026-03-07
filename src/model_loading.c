#include "model_loading.h"

#define VEC_STARTING_SIZE 128
// #define VEC_INLINE_FUNCTIONS
#include "vec.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_SYMBOL 64
#define MAX_ELEMENTS 8
#define BUF_STARTING_SIZE 4

VEC(Vertex, VertexBuf)
VEC(uint32_t, IntBuf)
VEC(vec2s, Vec2Buf)
VEC(vec3s, Vec3Buf)

VertexBuf vertices = {0};
IntBuf indices = {0};

IntBuf tmp_vertex_uv_indices = {0};
Vec3Buf tmp_normals = {0};
Vec2Buf tmp_uvs = {0};

#define SYS_ERR(result)                                                        \
    if ((result) < 0)                                                          \
        return MLD_SYSTEM_ERROR;

static inline MldResult load_obj(size_t file_size,
                                 char *file_data,
                                 MldMesh *out_mesh,
                                 MldStorageType storage) {

    VertexBuf tmp_vertex_buf = {0};
    IntBuf tmp_index_buf = {0};
    VertexBuf *vertex_buf = NULL;
    IntBuf *index_buf = NULL;

    switch (storage) {

    case MLD_STORAGE_FAST: {
        vertex_buf = &vertices;
        index_buf = &indices;
    } break;

    case MLD_STORAGE_MALLOC: {
        VertexBuf_init(&tmp_vertex_buf);
        IntBuf_init(&tmp_index_buf);
        vertex_buf = &tmp_vertex_buf;
        index_buf = &tmp_index_buf;
    } break;
    }

    size_t vertices_start = vertex_buf->count;
    size_t indices_start = index_buf->count;
    tmp_vertex_uv_indices.count = 0;
    tmp_normals.count = 0;
    tmp_uvs.count = 0;

    char *symbol_start = file_data;
    char symbol[MAX_ELEMENTS][MAX_SYMBOL] = {0};
    char index_str[MAX_SYMBOL] = {0};
    uint32_t param_count = 0;

    for (char *cursor = file_data; cursor < file_data + file_size; cursor++) {

        if (*cursor != ' ' && *cursor != '\n')
            continue;

        memcpy(symbol[param_count], symbol_start, cursor - symbol_start);
        symbol[param_count][cursor - symbol_start] = 0;

        symbol_start = cursor + 1;

        if (++param_count >= MAX_ELEMENTS)
            return MLD_INVALID_FILE_FORMAT;

        if (*cursor != '\n')
            continue;

        // Vertex
        if (!strcmp(symbol[0], "v")) {

            if (param_count < 4)
                return MLD_INVALID_FILE_FORMAT;

            Vertex vertex = {
                .pos = {.x = atof(symbol[1]),
                        .y = atof(symbol[2]),
                        .z = atof(symbol[3])},
                .color = {.r = 1.0, .g = 1.0, .b = 1.0},
            };

            if (param_count == 7)
                vertex.color = (vec3s){.r = atof(symbol[4]),
                                       .g = atof(symbol[5]),
                                       .b = atof(symbol[6])};

            SYS_ERR(VertexBuf_append(vertex_buf, &vertex));
            uint32_t max = UINT32_MAX;
            SYS_ERR(IntBuf_append(&tmp_vertex_uv_indices, &max));
        }

        // Vertex normal
        if (!strcmp(symbol[0], "vn")) {
            if (param_count != 4)
                return MLD_INVALID_FILE_FORMAT;

            vec3s vertex_normal = {.x = atof(symbol[1]),
                                   .y = atof(symbol[2]),
                                   .z = atof(symbol[3])};
            SYS_ERR(Vec3Buf_append(&tmp_normals, &vertex_normal));
        }

        // UV coordinate
        if (!strcmp(symbol[0], "vt")) {
            if (param_count > 4 || param_count < 2)
                return MLD_INVALID_FILE_FORMAT;

            vec2s uv = {.u = atof(symbol[1])};
            if (param_count >= 3)
                uv.v = atof(symbol[2]);

            SYS_ERR(Vec2Buf_append(&tmp_uvs, &uv));
        }

        param_count = 0;
    }

    symbol_start = file_data;
    param_count = 0;

    for (char *cursor = file_data; cursor < file_data + file_size; cursor++) {

        if (*cursor != ' ' && *cursor != '\n')
            continue;

        memcpy(symbol[param_count], symbol_start, cursor - symbol_start);
        symbol[param_count][cursor - symbol_start] = 0;

        symbol_start = cursor + 1;

        if (++param_count >= MAX_ELEMENTS)
            return MLD_INVALID_FILE_FORMAT;

        if (*cursor != '\n')
            continue;

        // Face data
        if (!strcmp(symbol[0], "f")) {

            if (param_count != 4)
                return MLD_NON_TRIANGLE_FACE;

            for (uint8_t i = 0; i < 3; i++) {
                char *group = symbol[i + 1];
                char *l = strchr(group, '/');
                char *r = strrchr(group, '/');
                if (l == r || l == NULL || r == NULL)
                    return MLD_INVALID_FILE_FORMAT;

                memcpy(index_str, group, l - group);
                index_str[l - group] = 0;
                uint32_t vert_index = atoi(index_str) - 1;

                Vertex *vertex =
                    VertexBuf_get(vertex_buf, vertices_start + vert_index);
                if (vertex == NULL)
                    return MLD_INVALID_FILE_FORMAT;

                memcpy(index_str, l + 1, r - l - 1);
                index_str[r - l - 1] = 0;
                uint32_t uv_index = atoi(index_str) - 1;
                uint32_t normal_index = atoi(r + 1) - 1;

                vec2s *uv = Vec2Buf_get(&tmp_uvs, uv_index);
                vec3s *normal = Vec3Buf_get(&tmp_normals, normal_index);

                if (uv == NULL || normal == NULL)
                    return MLD_INVALID_FILE_FORMAT;

                uint32_t *old_uv_index =
                    IntBuf_get(&tmp_vertex_uv_indices, vert_index);
                if (!old_uv_index)
                    return MLD_SYSTEM_ERROR;

                // Different uv coords -> duplicate vertex
                if (*old_uv_index != uv_index) {

                    vert_index = vertex_buf->count - vertices_start;
                    Vertex new_vertex = *vertex;
                    SYS_ERR(VertexBuf_append(vertex_buf, &new_vertex));
                    uint32_t max = UINT32_MAX;
                    SYS_ERR(IntBuf_append(&tmp_vertex_uv_indices, &max));

                    vertex = VertexBuf_get(vertex_buf, vertex_buf->count - 1);
                    if (vertex == NULL)
                        return MLD_SYSTEM_ERROR;
                    old_uv_index =
                        IntBuf_get(&tmp_vertex_uv_indices, vert_index);
                    if (old_uv_index == NULL)
                        return MLD_SYSTEM_ERROR;
                }

                // v coord needs to be flipped
                vertex->uv = (vec2s){.u = uv->u, .v = 1.0 - uv->v};
                vertex->normal = *normal;

                *old_uv_index = uv_index;

                SYS_ERR(IntBuf_append(index_buf, &vert_index));
            }
        }

        param_count = 0;
    }

    *out_mesh =
        (MldMesh){.mesh =
                      {
                          .vertex_count = vertex_buf->count - vertices_start,
                          .index_count = index_buf->count - indices_start,
                          .vertices = vertex_buf->data + vertices_start,
                          .indices = index_buf->data + indices_start,
                      },
                  .storage = storage};
    return MLD_SUCCESS;
}

MldResult
mld_load_file(const char *filepath, MldMesh *out_mesh, MldStorageType storage) {

    char *extension = strrchr(filepath, '.');

    if (!extension)
        return MLD_UNSUPPORTED_FILE_TYPE;

    MldResult result = MLD_UNSUPPORTED_FILE_TYPE;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0)
        return MLD_SYSTEM_ERROR;

    struct stat file_info;
    if (fstat(fd, &file_info) < 0) {
        close(fd);
        return MLD_SYSTEM_ERROR;
    }

    size_t file_size = file_info.st_size;
    char *file_buf = malloc(file_size);
    if (file_buf == NULL) {
        close(fd);
        return MLD_SYSTEM_ERROR;
    }

    int n;
    size_t size_read = 0;

    while (size_read < file_size) {
        n = read(fd, file_buf + size_read, file_size - size_read);
        if (n < 0) {
            close(fd);
            free(file_buf);
            return MLD_SYSTEM_ERROR;
        }
        if (n == 0)
            break;
        size_read += n;
    };

    close(fd);

    if (!strcmp(extension, ".obj"))
        result = load_obj(file_size, file_buf, out_mesh, storage);

    free(file_buf);
    return result;
}

MldResult mld_init(void) {

    SYS_ERR(VertexBuf_init(&vertices));
    SYS_ERR(IntBuf_init(&indices));
    SYS_ERR(Vec3Buf_init(&tmp_normals));
    SYS_ERR(IntBuf_init(&tmp_vertex_uv_indices));
    SYS_ERR(Vec2Buf_init(&tmp_uvs));

    return MLD_SUCCESS;
}

void mld_free(void) {

    VertexBuf_free(&vertices);
    IntBuf_free(&indices);
    Vec3Buf_free(&tmp_normals);
    IntBuf_free(&tmp_vertex_uv_indices);
    Vec2Buf_free(&tmp_uvs);
}

const char *mld_strerror(MldResult result) {

    switch (result) {

    case MLD_SUCCESS:
        return "Success";
    case MLD_INVALID_FILE_FORMAT:
        return "Invalid file format";
    case MLD_UNSUPPORTED_FILE_TYPE:
        return "Unsupported file type";
    case MLD_NON_TRIANGLE_FACE:
        return "Mesh contains non-triangulated faces";
    case MLD_SYSTEM_ERROR:
        return strerror(errno);
    case MLD_CANNOT_FREE_FAST_STORAGE_MESH:
        return "Cannot free a mesh with storage type MLD_STORAGE_FAST, use "
               "MLD_STORAGE_MALLOC instead";
        break;
    }
}
