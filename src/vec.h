#ifndef _VEC
#define _VEC

/*
This file includes two macros to generate dynamic array implementations for any
arbitrary fixed-size datatype.

VEC_DECLARE(datatype, name, prefix)
This will generate the struct and function declarations for the implementation.
You usually want this in a header file. The functions will be prefixed with
`prefix`, for example prefix value of 'intvec' will have functions such as
intvec_init, intvec_append, intvec_get and intvec_free. The generated type will
have name `name`.

VEC_IMPLEMENT(datatype, name, prefix)
This will provide implementations for the functions declared using the previous
macro. You usually want this in a source file.

VEC(datatype, name)
This is an alias for doing both of the aforementioned things, using name as the
prefix.

The default starting size of the vector can be overridden with
VEC_STARTING_SIZE.

Functions:

int {prefix}_init({name} *out_vec)
Will initialize the datatype. Use this before any of the other functions.
Returns -1 on error.

int {prefix}_append({name} *vec, {datatype} data)
Will append `data` to the end of the dynamic array `vec`.
Returns -1 on error.

{prefix}_get({name} *vec, size_t {index})
Will get an element from the dynamic array `vec` at `index`. Returns a null
pointer if index is out of range.

{prefix}_free({name} *vec)
Will free memory associated with this datatype.

*/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef VEC_STARTING_SIZE
#define VEC_STARTING_SIZE 8
#endif

#ifdef VEC_INLINE_FUNCTIONS
#define _VEC_INLINE inline
#else
#define _VEC_INLINE
#endif

#define VEC_DECLARE(datatype, name, prefix)                                    \
    typedef struct {                                                           \
        datatype *data;                                                        \
        size_t allocated;                                                      \
        size_t count;                                                          \
    } name;                                                                    \
    int prefix##_init(name *out_vec);                                          \
    int prefix##_append(name *vec, datatype *data);                            \
    datatype *prefix##_get(name *vec, size_t index);                           \
    void prefix##_free(name *vec);

#define VEC_IMPLEMENT(datatype, name, prefix)                                  \
    _VEC_INLINE int prefix##_init(name *out_vec) {                             \
        *out_vec = (name){                                                     \
            .data = malloc(VEC_STARTING_SIZE * sizeof(datatype)),              \
            .allocated = VEC_STARTING_SIZE,                                    \
        };                                                                     \
        if (!out_vec->data)                                                    \
            return -1;                                                         \
        return 0;                                                              \
    }                                                                          \
    _VEC_INLINE int prefix##_append(name *vec, datatype *data) {               \
        if (vec->count >= vec->allocated) {                                    \
            vec->allocated *= 2;                                               \
            vec->data = realloc(vec->data, vec->allocated * sizeof(datatype)); \
            if (!vec->data)                                                    \
                return -1;                                                     \
        }                                                                      \
        vec->data[vec->count++] = *data;                                       \
        return 0;                                                              \
    }                                                                          \
    _VEC_INLINE datatype *prefix##_get(name *vec, size_t index) {              \
        if (index >= vec->count)                                               \
            return 0;                                                          \
        return vec->data + index;                                              \
    }                                                                          \
    _VEC_INLINE void prefix##_free(name *vec) {                                \
        if (vec->data) {                                                       \
            free(vec->data);                                                   \
            memset(vec, 0, sizeof *vec);                                       \
        }                                                                      \
    }

#define VEC(datatype, name)                                                    \
    VEC_DECLARE(datatype, name, name) VEC_IMPLEMENT(datatype, name, name)

#endif
