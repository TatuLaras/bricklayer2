/*
 * A module to handle loading of model files.
 * The idea is that this module will also be the module to own all
 * the vertex data on the CPU. Aka. some graphics API interface will get the
 * data from this module to send to the GPU.
 *
 * I've made allocating the internal memory explicit by calling mld_init()
 * before any other function as well as calling mld_free() at the end.
 *
 * Currently only triangulated .obj files are supported.
 */
#ifndef _MODEL_LOADING
#define _MODEL_LOADING

#include <stdint.h>
#include <stdlib.h>

#include "cglm/types-struct.h"
#include "gapi_types.h"
#include "log.h"

#define STR(x) #x
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
} MldResult;

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
MldResult mld_load_file(const char *filepath, MeshData *out_mesh);
const char *mld_strerror(MldResult result);

#endif
