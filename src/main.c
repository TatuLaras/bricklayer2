#include "log.h"

#include <stdlib.h>
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vk_enum_string_helper.h>

#define WINDOW_HEIGHT 400
#define WINDOW_WIDTH 600

#define STR(x) #x

#define VK_ERR_MSG(result, message)                                            \
    {                                                                          \
        VkResult __res = result;                                               \
        if (__res != VK_SUCCESS) {                                             \
            ERROR(message ": %s", string_VkResult(__res));                     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    }
#define VK_ERR(result) VK_ERR_MSG(result, STR(result))

static inline GLFWwindow *init_window(int height, int width,
                                      const char *title) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(height, width, title, NULL, NULL);

    if (!window) {
        const char *msg = 0;
        glfwGetError(&msg);
        ERROR("%s", msg);
        exit(EXIT_FAILURE);
    }

    return window;
}

static inline void createInstance(void) {
#ifdef DEBUG
    char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

    // See if validation layer is supported
    uint32_t property_count = 0;
    VkLayerProperties *layer_properties;

    VK_ERR(vkEnumerateInstanceLayerProperties(&property_count, NULL));
    ERR((layer_properties =
             calloc(-property_count, sizeof(VkLayerProperties))) == 0,
        "calloc()");
    VK_ERR(
        vkEnumerateInstanceLayerProperties(&property_count, layer_properties));

    int layer_supported = 0;
    for (uint32_t i = 0; i < property_count; i++) {
        if (!strcmp(validation_layer_name, layer_properties[i].layerName)) {
            layer_supported = 1;
            break;
        }
    }

    if (!layer_supported) {
        ERROR("validation layer %s is not supported", validation_layer_name);
        exit(EXIT_FAILURE);
    }

    free(layer_properties);
#endif

    // Get Vulkan extensions required for GLWF window surface
    uint32_t glfw_extensions_count = 0;
    const char **glfw_extensions =
        glfwGetRequiredInstanceExtensions(&glfw_extensions_count);
    if (!glfw_extensions) {
        const char *msg = 0;
        glfwGetError(&msg);
        ERROR("%s", msg);
        exit(EXIT_FAILURE);
    }

    glfw_extensions =
        realloc(glfw_extensions, (glfw_extensions_count + 1) * sizeof(char *));
    glfw_extensions[glfw_extensions_count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "bricklayer2",
        .applicationVersion = 1,
        .apiVersion = VK_MAKE_VERSION(1, 4, 0),
    };
}

static inline void setupDebugMessenger(void) {}

static inline void createSurface(void) {}

static inline void pickPhysicalDevice(void) {}

static inline void createLogicalDevice(void) {}

static inline void createSwapChain(void) {}

static inline void createImageViews(void) {}

static inline void createGraphicsPipeline(void) {}

static inline void createCommandPool(void) {}

static inline void createCommandBuffer(void) {}

static inline void createSyncObjects(void) {}

int main(int argc, char **argv) {

    GLFWwindow *window =
        init_window(WINDOW_WIDTH, WINDOW_HEIGHT, "bricklayer2");

    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createGraphicsPipeline();
    createCommandPool();
    createCommandBuffer();
    createSyncObjects();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    exit(EXIT_SUCCESS);
    (void)argc;
    (void)argv;
}
