#ifndef _GRID
#define _GRID

#include "cglm/types.h"
#include "gapi.h"
#include <stdint.h>

// Create a gapi graphics pipeline for line rendering.
GapiResult grid_pipeline_create(GapiPipelineHandle *out_pipeline_handle);

// Creates a drawable grid object.
GapiResult grid_object_create(uint32_t size,
                              float density,
                              GapiObjectHandle *out_object_handle);

#endif
