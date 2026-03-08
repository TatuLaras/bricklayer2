#include "orbital_controls.h"
#include "cglm/quat.h"
#include "cglm/vec3.h"

#define ROTATE_SENSITIVITY_X 0.002
#define ROTATE_SENSITIVITY_Y 0.004
#define SHIFT_SENSITIVITY_X 0.0006
#define SHIFT_SENSITIVITY_Y 0.0006

#define BASE_ZOOM_SENSITIVITY 0.08
#define DRAG_ZOOM_SENSITIVITY_MULTIPLIER 0.010

void orbital_camera_update_zoom(GapiCamera *camera, float amount) {

    if (amount == 0)
        return;

    vec3 target_to_pos;
    glm_vec3_sub(camera->pos, camera->target, target_to_pos);

    amount = (amount * glm_vec3_norm(target_to_pos)) * BASE_ZOOM_SENSITIVITY;

    vec3 zoom_delta;
    glm_vec3_scale(target_to_pos, -1, zoom_delta);
    glm_normalize(zoom_delta);
    glm_vec3_scale(zoom_delta, amount, zoom_delta);

    glm_vec3_add(camera->pos, zoom_delta, camera->pos);
}

void orbital_camera_update(GapiCamera *camera,
                           double mouse_delta_x,
                           double mouse_delta_y,
                           OrbitalMode mode) {

    vec3 target_to_pos = {0};
    glm_vec3_sub(camera->pos, camera->target, target_to_pos);

    vec3 local_z_axis = {0};
    glm_vec3_scale(target_to_pos, -1, local_z_axis);
    glm_vec3_normalize(local_z_axis);

    vec3 local_x_axis = {0};
    glm_vec3_cross(local_z_axis, camera->up, local_x_axis);

    switch (mode) {

    case ORBITAL_NORMAL: {
        float yaw = -mouse_delta_x * ROTATE_SENSITIVITY_X;
        float pitch = -mouse_delta_y * ROTATE_SENSITIVITY_Y;

        versor yaw_q = {0};
        glm_quatv(yaw_q, yaw, (vec3){0, 1.0, 0});

        versor pitch_q = {0};
        glm_quatv(pitch_q, pitch, local_x_axis);

        versor total_q = {0};
        glm_quat_mul(pitch_q, yaw_q, total_q);

        glm_quat_rotatev(total_q, target_to_pos, target_to_pos);
        glm_vec3_add(camera->target, target_to_pos, camera->pos);
    } break;

    case ORBITAL_SHIFT_POSITION: {
        vec3 local_y_axis;
        glm_vec3_cross(local_z_axis, local_x_axis, local_y_axis);

        float shift_x =
            -mouse_delta_x * SHIFT_SENSITIVITY_X * glm_vec3_norm(target_to_pos);
        float shift_y =
            -mouse_delta_y * SHIFT_SENSITIVITY_Y * glm_vec3_norm(target_to_pos);

        vec3 vshift_x;
        glm_vec3_scale(local_x_axis, shift_x, vshift_x);
        vec3 vshift_y;
        glm_vec3_scale(local_y_axis, shift_y, vshift_y);

        glm_vec3_add(camera->target, vshift_x, camera->target);
        glm_vec3_add(camera->target, vshift_y, camera->target);
        glm_vec3_add(camera->pos, vshift_x, camera->pos);
        glm_vec3_add(camera->pos, vshift_y, camera->pos);
    } break;

    case ORBITAL_ZOOM: {
        float delta = mouse_delta_x;
        orbital_camera_update_zoom(camera,
                                   delta * DRAG_ZOOM_SENSITIVITY_MULTIPLIER);
    } break;
    }
}
