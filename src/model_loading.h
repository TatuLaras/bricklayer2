/*
 * A module to handle loading of model files.
 * The idea is that this module will also be the module to own all
 * the vertex data on the CPU.
 *
 * I've made allocating the internal memory explicit by calling mld_init()
 * before any other function as well as calling mld_free() at the end.
 *
 * Currently only triangulated .obj files are supported.
 */
#ifndef _MODEL_LOADING
#define _MODEL_LOADING

#include "log.h"
#include "types.h"
#include <stdint.h>
#include <stdlib.h>

#define MLD_ERR_MSG(result, message)                                           \
    {                                                                          \
        MldResult __res = result;                                              \
        if (__res != MLD_SUCCESS) {                                            \
            ERROR(message ": %s", mld_strerror(__res));                        \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    }
#define MLD_ERR(result) MLD_ERR_MSG(result, STR(result))

typedef enum {
    MLD_SUCCESS = 0,
    MLD_INVALID_FILE_FORMAT,
    MLD_UNSUPPORTED_FILE_TYPE,
    MLD_SYSTEM_ERROR,
    MLD_NON_TRIANGLE_FACE,
    MLD_CANNOT_FREE_FAST_STORAGE_MESH,
} MldResult;

/*
 * There's two types of storage types for mesh vertex and index data.
 *
 * Either you have all the vertex and index data in one big buffer which is more
 * performant. The downside is that you cannot free an individual mesh, so for
 * something like hot reloading the mesh data just keeps piling up. This option
 * (MLD_STORAGE_FAST) is ideally for the shippable release build of the game
 * where scene models are usually not repeatedly reloaded.
 *
 * The second option is doing individual mallocs for each mesh. This is more
 * suitable for the hot-reloading scenario and during development, when
 * performance is not a huge concern. These types of meshes are not freed when
 * mld_free() is called, so you have to free the malloced index and vertex data
 * yourself.
 */
typedef enum {
    MLD_STORAGE_FAST = 0,
    MLD_STORAGE_MALLOC,
} MldStorageType;

typedef struct {
    Mesh mesh;
    MldStorageType storage;
} MldMesh;

// Allocates memory for vertex data.
MldResult mld_init(void);
// Frees memory for vertex data.
void mld_free(void);

// Loads model file from `filepath`, storing the data in memory owned by this
// module which needs to be initialized with mld_init() and eventually be freed
// with mld_free().
// A Mesh (view into vertex and index data) will be written to `out_mesh`.
//
// Returns the error code.
//
// Supported file formats: .obj
MldResult
mld_load_file(const char *filepath, MldMesh *out_mesh, MldStorageType storage);
const char *mld_strerror(MldResult result);

#endif
