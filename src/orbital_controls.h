#ifndef _ORBITAL_CONTROLS
#define _ORBITAL_CONTROLS

#include "gapi_types.h"

typedef enum {
    ORBITAL_NORMAL = 0,
    ORBITAL_SHIFT_POSITION,
    ORBITAL_ZOOM,
} OrbitalMode;

void orbital_camera_update_zoom(GapiCamera *camera, float amount);
void orbital_camera_update(GapiCamera *camera,
                           double mouse_delta_x,
                           double mouse_delta_y,
                           OrbitalMode mode);

#endif
