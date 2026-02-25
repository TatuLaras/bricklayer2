#include "log.h"
#include "math_macros.h"

#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
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

#define SWAPCHAIN_MAX_IMAGES 12

static GLFWwindow *window = 0;
static VkInstance instance = 0;
static VkSurfaceKHR surface = 0;
static VkPhysicalDevice physical_device = 0;
static VkDevice logical_device = 0;
static uint32_t queue_index = 0;
static VkQueue queue = 0;
static VkSwapchainKHR swapchain = 0;

static struct {
    uint32_t count;
    VkImage images[SWAPCHAIN_MAX_IMAGES];
} swapchain_images = {.count = SWAPCHAIN_MAX_IMAGES};

static inline void init_window(int height, int width, const char *title) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(height, width, title, NULL, NULL);

    if (!window) {
        const char *msg = 0;
        glfwGetError(&msg);
        ERROR("%s", msg);
        exit(EXIT_FAILURE);
    }
}

static inline void create_instance(void) {
#ifdef DEBUG
    const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

    // See if validation layer is supported
    uint32_t property_count = 0;
    VkLayerProperties *layer_properties;

    VK_ERR(vkEnumerateInstanceLayerProperties(&property_count, NULL));
    ERR((layer_properties =
             calloc(property_count, sizeof(VkLayerProperties))) == 0,
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
    uint32_t required_extensions_count = 0;
    const char **glfw_extensions =
        glfwGetRequiredInstanceExtensions(&required_extensions_count);
    if (!glfw_extensions) {
        const char *msg = 0;
        glfwGetError(&msg);
        ERROR("%s", msg);
        exit(EXIT_FAILURE);
    }

    const char **required_extensions =
        malloc(required_extensions_count * sizeof(char *));
    memcpy(required_extensions, glfw_extensions,
           required_extensions_count * sizeof(char *));

#ifdef DEBUG
    required_extensions = realloc(
        required_extensions, (required_extensions_count + 1) * sizeof(char *));
    required_extensions[required_extensions_count++] =
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif

    uint32_t extension_properties_count = 0;
    VkExtensionProperties *extension_properties;
    VK_ERR(vkEnumerateInstanceExtensionProperties(
        NULL, &extension_properties_count, NULL));
    ERR((extension_properties = calloc(extension_properties_count,
                                       sizeof(VkExtensionProperties))) == 0,
        "calloc()");
    VK_ERR(vkEnumerateInstanceExtensionProperties(
        NULL, &extension_properties_count, extension_properties));

    for (uint32_t required_i = 0; required_i < required_extensions_count;
         required_i++) {

        int extension_supported = 0;
        for (uint32_t supported_i = 0; supported_i < extension_properties_count;
             supported_i++) {

            if (!strcmp(required_extensions[required_i],
                        extension_properties[supported_i].extensionName)) {
                extension_supported = 1;
                break;
            }
        }

        if (!extension_supported) {
            ERROR("extension %s not supported",
                  required_extensions[required_i]);
            exit(EXIT_FAILURE);
        }
    }

    free(extension_properties);

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "bricklayer2",
        .applicationVersion = 1,
        .apiVersion = VK_MAKE_VERSION(1, 4, 0),
    };
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
#ifdef DEBUG
        .enabledLayerCount = 1,
        .ppEnabledLayerNames = &validation_layer_name,
#endif
        .enabledExtensionCount = required_extensions_count,
        .ppEnabledExtensionNames = required_extensions,
    };
    VK_ERR(vkCreateInstance(&create_info, NULL, &instance));
    free(required_extensions);
}

static inline int find_queue(VkPhysicalDevice device,
                             uint32_t *out_queue_index) {

    uint32_t queue_count = 0;
    VkQueueFamilyProperties *queues;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, NULL);
    ERR((queues = calloc(queue_count, sizeof *queues)) == 0, "calloc()");
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues);

    for (uint32_t i = 0; i < queue_count; i++) {
        if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
            continue;

        VkBool32 is_present_supported;
        VK_ERR(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface,
                                                    &is_present_supported));

        if (is_present_supported == VK_FALSE)
            continue;

        *out_queue_index = i;
        free(queues);
        return 0;
    }

    free(queues);
    return -1;
}

static inline void pick_physical_device(void) {

    uint32_t device_count = 0;
    VkPhysicalDevice *devices;
    VK_ERR(vkEnumeratePhysicalDevices(instance, &device_count, NULL));
    ERR((devices = calloc(device_count, sizeof *devices)) == 0, "calloc()");
    VK_ERR(vkEnumeratePhysicalDevices(instance, &device_count, devices));

    //  FIXME: Proper checking for device capabilities

    for (uint32_t i = 0; i < device_count; i++) {

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(devices[i], &device_properties);

        if (find_queue(devices[i], &queue_index) < 0)
            continue;

        physical_device = devices[i];
        free(devices);
        return;
    }

    free(devices);
    ERROR("no suitable GPU found");
    exit(EXIT_FAILURE);
}

static inline void create_logical_device(void) {
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT f1 = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .extendedDynamicState = VK_TRUE,
    };
    VkPhysicalDeviceVulkan13Features f2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &f1,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceVulkan11Features f3 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &f2,
        .shaderDrawParameters = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &f3,
    };

    float queue_priority = 0.5;
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char *required_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = &required_extension,
    };

    VK_ERR(vkCreateDevice(physical_device, &device_create_info, NULL,
                          &logical_device));
    vkGetDeviceQueue(logical_device, queue_index, 0, &queue);
}

static inline void create_swap_chain(void) {
    VkSurfaceCapabilitiesKHR surface_capabilities;
    VK_ERR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
                                                     &surface_capabilities));

    // Choose swap extent
    VkExtent2D swap_extent = surface_capabilities.currentExtent;
    if (swap_extent.width == 0xffffffff) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        swap_extent.width =
            CLAMP((uint32_t)width, surface_capabilities.minImageExtent.width,
                  surface_capabilities.maxImageExtent.width);
        swap_extent.height =
            CLAMP((uint32_t)height, surface_capabilities.minImageExtent.height,
                  surface_capabilities.maxImageExtent.height);
    }

    // Choose image format
    uint32_t format_count = 0;
    VkSurfaceFormatKHR *formats;
    VK_ERR(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                                &format_count, NULL));
    ERR((formats = calloc(format_count, sizeof *formats)) == 0, "calloc()");
    VK_ERR(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                                &format_count, formats));

    ERR(format_count == 0, "no formats");

    VkSurfaceFormatKHR format = *formats;
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format != VK_FORMAT_B8G8R8A8_SRGB)
            continue;
        if (formats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            continue;
        format = formats[i];
        break;
    }

    free(formats);

    // Choose minimum image count
    uint32_t min_image_count = MAX(3, surface_capabilities.minImageCount);
    if (surface_capabilities.maxImageCount > 0 &&
        surface_capabilities.maxImageCount < min_image_count)
        min_image_count = surface_capabilities.maxImageCount;

    // Choose present mode
    uint32_t present_modes_count = 0;
    VkPresentModeKHR *present_modes;
    VK_ERR(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &present_modes_count, NULL));
    ERR((present_modes = calloc(present_modes_count, sizeof *present_modes)) ==
            0,
        "calloc()");
    VK_ERR(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &present_modes_count, present_modes));

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAX_ENUM_KHR;
    for (uint32_t i = 0; i < present_modes_count; i++) {
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = present_modes[i];
            break;
        }
        if (present_modes[i] == VK_PRESENT_MODE_FIFO_KHR)
            present_mode = present_modes[i];
    }

    if (present_mode == VK_PRESENT_MODE_MAX_ENUM_KHR) {
        ERROR("no suitable present mode found");
        exit(EXIT_FAILURE);
    }

    free(present_modes);

    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = min_image_count,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = swap_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
    };
    VK_ERR(vkCreateSwapchainKHR(logical_device, &swapchain_create_info, NULL,
                                &swapchain));

    VK_ERR(vkGetSwapchainImagesKHR(logical_device, swapchain,
                                   &swapchain_images.count,
                                   swapchain_images.images));
}

static inline void createImageViews(void) {}

static inline void createGraphicsPipeline(void) {}

static inline void createCommandPool(void) {}

static inline void createCommandBuffer(void) {}

static inline void createSyncObjects(void) {}

int main(int argc, char **argv) {

    init_window(WINDOW_WIDTH, WINDOW_HEIGHT, "bricklayer2");

    create_instance();

    VK_ERR(glfwCreateWindowSurface(instance, window, NULL, &surface));

    pick_physical_device();
    create_logical_device();

    create_swap_chain();

    createImageViews();
    createGraphicsPipeline();
    createCommandPool();
    createCommandBuffer();
    createSyncObjects();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    printf("Exiting\n");
    glfwDestroyWindow(window);
    glfwTerminate();

    exit(EXIT_SUCCESS);
    (void)argc;
    (void)argv;
}
