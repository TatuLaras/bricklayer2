#include "user_input.h"

#include <string.h>

typedef struct {
    double x;
    double y;
} MousePos;

static GLFWwindow *glfw_window = NULL;

static uint8_t mouse_button_down[UIN_MOUSE_BUTTON_LAST] = {0};
static uint8_t mouse_button_pressed[UIN_MOUSE_BUTTON_LAST] = {0};
static uint8_t mouse_button_released[UIN_MOUSE_BUTTON_LAST] = {0};

static uint8_t key_down[UIN_KEY_LAST] = {0};
static uint8_t key_pressed[UIN_KEY_LAST] = {0};
static uint8_t key_released[UIN_KEY_LAST] = {0};

static MousePos current_mouse = {0};
static MousePos previous_mouse = {0};

static double scroll_x = 0;
static double scroll_y = 0;

static void
mouse_button_callback(GLFWwindow *_window, int button, int action, int mods) {
    (void)_window;
    (void)mods;

    int is_down = action == GLFW_PRESS;
    mouse_button_down[button] = is_down;
    mouse_button_pressed[button] = is_down;
    mouse_button_released[button] = !is_down;
}

static void
key_callback(GLFWwindow *_window, int key, int scancode, int action, int mods) {
    (void)_window;
    (void)mods;
    (void)scancode;

    int is_down = action == GLFW_PRESS;
    key_down[key] = is_down;
    key_pressed[key] = is_down;
    key_released[key] = !is_down;
}

static void scroll_callback(GLFWwindow *_window, double x, double y) {
    (void)_window;
    (void)y;

    scroll_x = x;
    scroll_y = y;
}

void uin_init(GLFWwindow *window) {

    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfw_window = window;
}

void uin_refresh(void) {

    memset(mouse_button_pressed, 0, sizeof mouse_button_pressed);
    memset(mouse_button_released, 0, sizeof mouse_button_released);
    memset(key_pressed, 0, sizeof key_pressed);
    memset(key_released, 0, sizeof key_released);

    scroll_y = 0;
    scroll_x = 0;

    previous_mouse = current_mouse;
    glfwGetCursorPos(glfw_window, &current_mouse.x, &current_mouse.y);
}

int uin_is_mouse_button_down(UinMouseButton button) {
    return mouse_button_down[button];
}
int uin_is_mouse_button_pressed(UinMouseButton button) {
    return mouse_button_pressed[button];
}
int uin_is_mouse_button_released(UinMouseButton button) {
    return mouse_button_released[button];
}
int uin_is_key_down(UinKey key) {
    return key_down[key];
}
int uin_is_key_pressed(UinKey key) {
    return key_pressed[key];
}
int uin_is_key_released(UinKey key) {
    return key_released[key];
}

void uin_set_cursor(UinCursorMode mode) {
    glfwSetInputMode(glfw_window, GLFW_CURSOR, mode);
}

void uin_get_mouse_pos(double *out_x, double *out_y) {
    glfwGetCursorPos(glfw_window, out_x, out_y);
}

void uin_get_mouse_delta(double *out_x, double *out_y) {
    *out_x = current_mouse.x - previous_mouse.x;
    *out_y = current_mouse.y - previous_mouse.y;
}

float uin_get_scroll(void) {
    return scroll_y;
}

float uin_get_scroll_horizontal(void) {
    return scroll_x;
}
