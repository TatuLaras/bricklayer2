#include "graphics_api.h"

#include "cglm/cglm.h"
#include "log.h"
#include "math_macros.h"
// #define VEC_INLINE_FUNCTIONS
#include "vec.h"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define STR(x) #x
// #define VK_ERR_MSG(result, message)                                            \
//     {                                                                          \
//         VkResult __res = result;                                               \
//         if (__res != VK_SUCCESS) {                                             \
//             ERROR(message ": %s", string_VkResult(__res));                     \
//             exit(EXIT_FAILURE);                                                \
//         }                                                                      \
//     }
// #define VK_ERR(result) VK_ERR_MSG(result, STR(result))

#define SYS_ERR(result)                                                        \
    if ((result) < 0)                                                          \
        return GFX_SYSTEM_ERROR;

#define GAPI_DEBUG_PRINT // TODO: remove

#ifdef GAPI_DEBUG_PRINT
#define VK_ERR(result)                                                         \
    {                                                                          \
        VkResult __res = result;                                               \
        if (__res != VK_SUCCESS) {                                             \
            ERROR(STR(result) ": %s", string_VkResult(__res));                 \
            vulkan_error = __res;                                              \
            return GFX_VULKAN_ERROR;                                           \
        }                                                                      \
    }
#else
#define VK_ERR(result)                                                         \
    {                                                                          \
        VkResult __res = result;                                               \
        if (__res != VK_SUCCESS) {                                             \
            vulkan_error = __res;                                              \
            return GFX_VULKAN_ERROR;                                           \
        }                                                                      \
    }
#endif

#define PROPAGATE(result)                                                      \
    {                                                                          \
        GfxResult __res = result;                                              \
        if (__res != GFX_SUCCESS) {                                            \
            return __res;                                                      \
        }                                                                      \
    }

#define MAX_FRAMES_IN_FLIGHT 2
#define SWAPCHAIN_MAX_IMAGES 16

typedef struct {
    mat4 model;
    mat4 view;
    mat4 projection;
} UBOData;

typedef struct {
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    VkDeviceMemory vertex_memory;
    VkDeviceMemory index_memory;
    uint32_t index_count;
} GapiMesh;

typedef struct {
    GapiMeshHandle mesh_handle;
    vec4 model_matrix[4];
    VkBuffer uniform_buffers[MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory uniform_buffer_memories[MAX_FRAMES_IN_FLIGHT];
    void *uniform_buffer_mappings[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT];
} GapiObject;

typedef struct {
    VkImage image;
    VkDeviceMemory image_memory;
    VkImageView image_view;
    VkSampler sampler;
} GapiTexture;

VEC(GapiObject, GapiObjectBuf)
VEC(GapiMesh, GapiMeshBuf)
VEC(GapiTexture, GapiTextureBuf)

static VkResult vulkan_error;
static uint32_t frame_index = 0;
static uint32_t image_index;

static GLFWwindow *window = NULL;
static int has_window_resized = 0;

static VkInstance instance = NULL;
static VkSurfaceKHR surface = NULL;
static VkSurfaceFormatKHR surface_format = {0};
static VkPhysicalDevice physical_device = NULL;
static VkDevice logical_device = NULL;
static uint32_t queue_index = 0;
static VkQueue queue = 0;
static VkExtent2D swap_extent = {0};
static VkSwapchainKHR swapchain = 0;
static struct {
    uint32_t count;
    VkImage images[SWAPCHAIN_MAX_IMAGES];
    VkImageView image_views[SWAPCHAIN_MAX_IMAGES];
} swapchain_images = {.count = SWAPCHAIN_MAX_IMAGES};

static VkSemaphore present_done_semaphores[MAX_FRAMES_IN_FLIGHT] = {0};
static VkSemaphore rendering_done_semaphores[MAX_FRAMES_IN_FLIGHT] = {0};
static VkFence draw_fences[MAX_FRAMES_IN_FLIGHT] = {0};

static VkCommandPool command_pool = NULL;
static VkCommandBuffer drawing_command_buffers[MAX_FRAMES_IN_FLIGHT] = {0};

static VkImage depth_image = NULL;
static VkDeviceMemory depth_image_memory;
static VkImageView depth_image_view;
static VkFormat depth_format = 0;

static VkDescriptorPool descriptor_pool = NULL;
static VkDescriptorSetLayout descriptor_set_layout = NULL;

static VkPipeline *pipelines = NULL;
static VkPipelineLayout pipeline_layout = NULL;

static GapiObjectBuf objects = {0};
static GapiMeshBuf meshes = {0};
static GapiTextureBuf textures = {0};

static void
window_resize_callback(GLFWwindow *_window, int _width, int _height) {
    (void)_window;
    (void)_width;
    (void)_height;

    has_window_resized = 1;
}

static inline GfxResult init_window(uint32_t width,
                                    uint32_t height,
                                    const char *title,
                                    GfxWindowFlags flags) {

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    if (flags & GFX_WINDOW_RESIZEABLE)
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    else
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(width, height, title, NULL, NULL);

    if (!window) {
        const char *msg = 0;
        glfwGetError(&msg);
        ERROR("%s", msg);
        return GFX_GLFW_ERROR;
    }
    glfwSetFramebufferSizeCallback(window, window_resize_callback);

    return GFX_SUCCESS;
}

static inline GfxResult create_instance(void) {
#ifdef DEBUG
    const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

    // See if validation layer is supported
    uint32_t property_count = 0;
    VK_ERR(vkEnumerateInstanceLayerProperties(&property_count, NULL));

    VkLayerProperties layer_properties[property_count];
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
        return GFX_VULKAN_FEATURE_UNSUPPORTED;
    }

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
    memcpy(required_extensions,
           glfw_extensions,
           required_extensions_count * sizeof(char *));

#ifdef DEBUG
    required_extensions = realloc(
        required_extensions, (required_extensions_count + 1) * sizeof(char *));
    required_extensions[required_extensions_count++] =
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif

    uint32_t extension_properties_count = 0;
    VK_ERR(vkEnumerateInstanceExtensionProperties(
        NULL, &extension_properties_count, NULL));

    VkExtensionProperties extension_properties[extension_properties_count];
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
            return GFX_VULKAN_FEATURE_UNSUPPORTED;
        }
    }

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

    return GFX_SUCCESS;
}

static inline GfxResult find_queue(VkPhysicalDevice device,
                                   uint32_t *out_queue_index) {

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, NULL);

    VkQueueFamilyProperties queues[queue_count];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues);

    for (uint32_t i = 0; i < queue_count; i++) {
        if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
            continue;

        VkBool32 is_present_supported;
        VK_ERR(vkGetPhysicalDeviceSurfaceSupportKHR(
            device, i, surface, &is_present_supported));

        if (is_present_supported == VK_FALSE)
            continue;

        *out_queue_index = i;
        return GFX_SUCCESS;
    }

    return GFX_ERROR_GENERIC;
}

static inline GfxResult pick_physical_device(void) {

    uint32_t device_count = 0;
    VK_ERR(vkEnumeratePhysicalDevices(instance, &device_count, NULL));

    VkPhysicalDevice devices[device_count];
    VK_ERR(vkEnumeratePhysicalDevices(instance, &device_count, devices));

    //  FIXME: Proper checking for device capabilities

    for (uint32_t i = 0; i < device_count; i++) {

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(devices[i], &device_properties);

        if (find_queue(devices[i], &queue_index) != GFX_SUCCESS)
            continue;

        physical_device = devices[i];
        return GFX_SUCCESS;
    }

    ERROR("no suitable GPU found");
    return GFX_NO_DEVICE_FOUND;
}

static inline GfxResult create_logical_device(void) {
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
        .features = {.samplerAnisotropy = VK_TRUE},
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

    VK_ERR(vkCreateDevice(
        physical_device, &device_create_info, NULL, &logical_device));
    vkGetDeviceQueue(logical_device, queue_index, 0, &queue);

    return GFX_SUCCESS;
}

static inline GfxResult create_swapchain(void) {

    VkSurfaceCapabilitiesKHR surface_capabilities;
    VK_ERR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device, surface, &surface_capabilities));

    // Choose swap extent
    swap_extent = surface_capabilities.currentExtent;
    if (swap_extent.width == 0xffffffff) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        swap_extent.width = CLAMP((uint32_t)width,
                                  surface_capabilities.minImageExtent.width,
                                  surface_capabilities.maxImageExtent.width);
        swap_extent.height = CLAMP((uint32_t)height,
                                   surface_capabilities.minImageExtent.height,
                                   surface_capabilities.maxImageExtent.height);
    }

    // Choose image format
    uint32_t format_count = 0;
    VK_ERR(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device, surface, &format_count, NULL));

    VkSurfaceFormatKHR formats[format_count];
    VK_ERR(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical_device, surface, &format_count, formats));

    ERR(format_count == 0, "no formats");

    surface_format = *formats;
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format != VK_FORMAT_B8G8R8A8_SRGB)
            continue;
        if (formats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            continue;
        surface_format = formats[i];
        break;
    }

    // Choose minimum image count
    uint32_t min_image_count = MAX(3, surface_capabilities.minImageCount);
    if (surface_capabilities.maxImageCount > 0 &&
        surface_capabilities.maxImageCount < min_image_count)
        min_image_count = surface_capabilities.maxImageCount;

    // Choose present mode
    uint32_t present_modes_count = 0;
    VK_ERR(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &present_modes_count, NULL));

    VkPresentModeKHR present_modes[present_modes_count];
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
        return GFX_VULKAN_FEATURE_UNSUPPORTED;
    }

    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = min_image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = swap_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = swapchain,
    };
    VK_ERR(vkCreateSwapchainKHR(
        logical_device, &swapchain_create_info, NULL, &swapchain));

    VK_ERR(vkGetSwapchainImagesKHR(logical_device,
                                   swapchain,
                                   &swapchain_images.count,
                                   swapchain_images.images));

    // Create image views
    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = surface_format.format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    for (uint32_t i = 0; i < swapchain_images.count; i++) {
        image_view_create_info.image = swapchain_images.images[i];
        VK_ERR(vkCreateImageView(logical_device,
                                 &image_view_create_info,
                                 NULL,
                                 swapchain_images.image_views + i));
    }

    return GFX_SUCCESS;
}

static inline GfxResult create_drawing_command_buffers(void) {

    // Create command pool
    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_index,
    };
    VK_ERR(vkCreateCommandPool(
        logical_device, &command_pool_create_info, NULL, &command_pool));

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    VK_ERR(vkAllocateCommandBuffers(
        logical_device, &alloc_info, drawing_command_buffers));

    return GFX_SUCCESS;
}

static inline GfxResult find_memory_type(VkMemoryRequirements requirements,
                                         VkMemoryPropertyFlags flags,
                                         uint32_t *out_memory_type) {

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    int selected_memory_type = -1;

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1 << i)) == 0)
            continue;
        if ((memory_properties.memoryTypes[i].propertyFlags & flags) != flags)
            continue;

        selected_memory_type = i;
        break;
    }

    if (selected_memory_type < 0)
        return GFX_VULKAN_FEATURE_UNSUPPORTED;

    *out_memory_type = selected_memory_type;
    return GFX_SUCCESS;
}

static inline GfxResult create_image(uint32_t width,
                                     uint32_t height,
                                     VkFormat format,
                                     VkImageTiling tiling,
                                     VkImageUsageFlags usage,
                                     VkMemoryPropertyFlags properties,
                                     VkImage *out_image,
                                     VkDeviceMemory *out_memory) {

    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_ERR(vkCreateImage(logical_device, &image_create_info, NULL, out_image));

    // Create memory
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(
        logical_device, *out_image, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
    };
    PROPAGATE(find_memory_type(
        memory_requirements, properties, &alloc_info.memoryTypeIndex));
    VK_ERR(vkAllocateMemory(logical_device, &alloc_info, NULL, out_memory));
    VK_ERR(vkBindImageMemory(logical_device, *out_image, *out_memory, 0));

    return GFX_SUCCESS;
}

static inline GfxResult find_supported_depth_format(uint32_t candidates_count,
                                                    VkFormat *candidates,
                                                    VkImageTiling tiling,
                                                    VkFormatFeatureFlags flags,
                                                    VkFormat *out_format) {

    for (uint32_t i = 0; i < candidates_count; i++) {

        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(
            physical_device, candidates[i], &properties);

        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (properties.linearTilingFeatures & flags) == flags) {
            *out_format = candidates[i];
            return GFX_SUCCESS;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (properties.optimalTilingFeatures & flags) == flags) {
            *out_format = candidates[i];
            return GFX_SUCCESS;
        }
    }

    return GFX_VULKAN_FEATURE_UNSUPPORTED;
}

static inline GfxResult create_depth_resources(void) {

    VkFormat format_candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    PROPAGATE(find_supported_depth_format(
        COUNT(format_candidates),
        format_candidates,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
        &depth_format));

    PROPAGATE(create_image(swap_extent.width,
                           swap_extent.height,
                           depth_format,
                           VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           &depth_image,
                           &depth_image_memory));

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depth_format,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    };
    VK_ERR(vkCreateImageView(
        logical_device, &image_view_create_info, NULL, &depth_image_view));

    return GFX_SUCCESS;
}

//  TODO: Depends on the shader used, dynamically create?
static inline GfxResult create_descriptor_set_layout(void) {

    VkDescriptorSetLayoutBinding layout_bindings[] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = layout_bindings,
    };
    VK_ERR(vkCreateDescriptorSetLayout(
        logical_device, &layout_create_info, NULL, &descriptor_set_layout));

    return GFX_SUCCESS;
}

static inline GfxResult create_descriptor_pool(void) {

    if (objects.count == 0)
        return GFX_SUCCESS;

    if (descriptor_pool != NULL) {
        vkDestroyDescriptorPool(logical_device, descriptor_pool, NULL);
        descriptor_pool = NULL;
    }

    // Create descriptor pool
    VkDescriptorPoolSize pool_sizes[] = {
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = MAX_FRAMES_IN_FLIGHT * objects.count},
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = MAX_FRAMES_IN_FLIGHT * objects.count},
    };
    VkDescriptorPoolCreateInfo pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = MAX_FRAMES_IN_FLIGHT * objects.count,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes,
    };
    VK_ERR(vkCreateDescriptorPool(
        logical_device, &pool_create_info, NULL, &descriptor_pool));

    return GFX_SUCCESS;
}

static inline GfxResult
create_descriptor_sets(uint32_t descriptor_set_count,
                       VkBuffer *uniform_buffers,
                       GapiTexture *texture,
                       VkDescriptorSet *out_descriptor_sets) {

    VkDescriptorSetLayout layouts[descriptor_set_count];
    for (uint32_t i = 0; i < descriptor_set_count; i++)
        layouts[i] = descriptor_set_layout;

    VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = descriptor_set_count,
        .pSetLayouts = layouts,
    };
    VK_ERR(vkAllocateDescriptorSets(
        logical_device, &descriptor_set_alloc_info, out_descriptor_sets));

    for (uint32_t i = 0; i < descriptor_set_count; i++) {

        VkDescriptorBufferInfo buf_info = {
            .buffer = uniform_buffers[i],
            .offset = 0,
            .range = sizeof(UBOData),
        };
        VkDescriptorImageInfo image_info = {
            .sampler = texture->sampler,
            .imageView = texture->image_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet descriptor_writes[] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = out_descriptor_sets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buf_info,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = out_descriptor_sets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_info,
            },
        };
        vkUpdateDescriptorSets(logical_device,
                               COUNT(descriptor_writes),
                               descriptor_writes,
                               0,
                               NULL);
    }

    return GFX_SUCCESS;
}

static inline GfxResult create_graphics_pipeline(GfxShader shader,
                                                 VkPipeline *out_pipeline) {

    VkShaderModule shader_module;
    VkShaderModuleCreateInfo shader_module_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader.size,
        .pCode = (uint32_t *)shader.code,
    };
    VK_ERR(vkCreateShaderModule(
        logical_device, &shader_module_create_info, NULL, &shader_module));

    VkPipelineShaderStageCreateInfo vert_shader_stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shader_module,
        .pName = "vertMain",
    };
    VkPipelineShaderStageCreateInfo frag_shader_stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = shader_module,
        .pName = "fragMain",
    };
    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage,
                                                       frag_shader_stage};

    VkVertexInputBindingDescription binding_description = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attribute_descriptions[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, pos),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, color),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, uv),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,

        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_description,

        .vertexAttributeDescriptionCount = COUNT(attribute_descriptions),
        .pVertexAttributeDescriptions = attribute_descriptions,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasSlopeFactor = 1.0,
        .lineWidth = 1.0,
    };
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                       VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = sizeof dynamic_states / sizeof *dynamic_states,
        .pDynamicStates = dynamic_states,
    };

    VkPipelineLayoutCreateInfo layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout,
        .pushConstantRangeCount = 0,
    };

    VK_ERR(vkCreatePipelineLayout(
        logical_device, &layout_create_info, NULL, &pipeline_layout));

    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &surface_format.format,
        .depthAttachmentFormat = depth_format,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };
    VkGraphicsPipelineCreateInfo graphics = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout,
        .renderPass = NULL,
    };
    VK_ERR(vkCreateGraphicsPipelines(
        logical_device, NULL, 1, &graphics, NULL, out_pipeline));

    vkDestroyShaderModule(logical_device, shader_module, NULL);

    return GFX_SUCCESS;
}

static inline GfxResult
create_buffer(VkDeviceSize size,
              VkBufferUsageFlags usage,
              VkMemoryPropertyFlags memory_property_flags,
              VkBuffer *out_buffer,
              VkDeviceMemory *out_memory) {

    // Create buffer
    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_ERR(
        vkCreateBuffer(logical_device, &buffer_create_info, NULL, out_buffer));

    // Get memory requirements and determine memory type
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(
        logical_device, *out_buffer, &memory_requirements);

    uint32_t selected_memory_type;
    PROPAGATE(find_memory_type(
        memory_requirements, memory_property_flags, &selected_memory_type));

    // Allocate and bind
    VkMemoryAllocateInfo memory_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = selected_memory_type,
    };
    VK_ERR(vkAllocateMemory(
        logical_device, &memory_allocate_info, NULL, out_memory));

    VK_ERR(vkBindBufferMemory(logical_device, *out_buffer, *out_memory, 0));

    return GFX_SUCCESS;
}

static inline GfxResult
begin_single_time_commands(VkCommandBuffer *command_buffer) {

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_ERR(
        vkAllocateCommandBuffers(logical_device, &alloc_info, command_buffer));

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_ERR(vkBeginCommandBuffer(*command_buffer, &begin_info));

    return GFX_SUCCESS;
}

static inline GfxResult
end_single_time_commands(VkCommandBuffer command_buffer) {

    VK_ERR(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    VK_ERR(vkQueueSubmit(queue, 1, &submit_info, NULL));
    VK_ERR(vkQueueWaitIdle(queue));

    return GFX_SUCCESS;
}

static inline void buffer_copy(VkBuffer dst, VkBuffer src, VkDeviceSize size) {

    VkCommandBuffer command_buffer;
    begin_single_time_commands(&command_buffer);

    // Copy command
    VkBufferCopy regions = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size,
    };
    vkCmdCopyBuffer(command_buffer, src, dst, 1, &regions);

    end_single_time_commands(command_buffer);
}

static inline void destroy_buffer(VkBuffer buffer, VkDeviceMemory memory) {
    vkFreeMemory(logical_device, memory, NULL);
    vkDestroyBuffer(logical_device, buffer, NULL);
}

static inline GfxResult upload_data(void *data,
                                    uint32_t size,
                                    VkBufferUsageFlagBits usage,
                                    VkBuffer *out_buffer,
                                    VkDeviceMemory *out_memory) {

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    PROPAGATE(create_buffer(size,
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &staging_buffer,
                            &staging_buffer_memory));

    PROPAGATE(create_buffer(size,
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            out_buffer,
                            out_memory));

    // Fill staging buffer
    void *mapped_data;
    VK_ERR(vkMapMemory(
        logical_device, staging_buffer_memory, 0, size, 0, &mapped_data));
    memcpy(mapped_data, data, size);
    vkUnmapMemory(logical_device, staging_buffer_memory);

    // Copy from staging buffer to actual buffer
    buffer_copy(*out_buffer, staging_buffer, size);

    VK_ERR(vkQueueWaitIdle(queue));
    destroy_buffer(staging_buffer, staging_buffer_memory);

    return GFX_SUCCESS;
}

static inline GfxResult create_uniform_buffer(VkBuffer *out_buffer,
                                              VkDeviceMemory *out_memory,
                                              void **out_mapping) {

    VkDeviceSize size = sizeof(UBOData);
    PROPAGATE(create_buffer(size,
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            out_buffer,
                            out_memory));

    VK_ERR(vkMapMemory(logical_device, *out_memory, 0, size, 0, out_mapping));

    return GFX_SUCCESS;
}

static inline void
transition_swapchain_image_layout(VkImageLayout old_layout,
                                  VkImageLayout new_layout,
                                  VkAccessFlags2 src_access_mask,
                                  VkAccessFlags2 dst_access_mask,
                                  VkPipelineStageFlags2 src_stage_mask,
                                  VkPipelineStageFlags2 dst_stage_mask) {

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain_images.images[image_index],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
    };
    VkDependencyInfo dependency_info = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };

    vkCmdPipelineBarrier2(drawing_command_buffers[frame_index],
                          &dependency_info);
}

static inline void
transition_image_layout(VkImage image,
                        VkImageLayout old_layout,
                        VkImageLayout new_layout,
                        VkAccessFlags src_access_mask,
                        VkAccessFlags dst_access_mask,
                        VkPipelineStageFlags src_stage,
                        VkPipelineStageFlags dst_stage,
                        VkImageAspectFlags image_aspect_flags) {

    VkCommandBuffer cmd_buf;
    begin_single_time_commands(&cmd_buf);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcAccessMask = src_access_mask,
        .dstAccessMask = dst_access_mask,
        .image = image,
        .subresourceRange = {image_aspect_flags, 0, 1, 0, 1},
    };

    vkCmdPipelineBarrier(
        cmd_buf, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);

    end_single_time_commands(cmd_buf);
}

static inline void copy_buffer_to_image(VkBuffer buffer,
                                        VkImage image,
                                        uint32_t width,
                                        uint32_t height) {

    VkCommandBuffer cmd_buf;
    begin_single_time_commands(&cmd_buf);

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(cmd_buf,
                           buffer,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    end_single_time_commands(cmd_buf);
}

static inline void destroy_depth_resources(void) {
    vkDestroyImage(logical_device, depth_image, NULL);
    vkFreeMemory(logical_device, depth_image_memory, NULL);
    vkDestroyImageView(logical_device, depth_image_view, NULL);
}

static inline void destroy_swapchain(void) {

    for (uint32_t i = 0; i < swapchain_images.count; i++) {
        vkDestroyImageView(
            logical_device, swapchain_images.image_views[i], NULL);
    }

    vkDestroySwapchainKHR(logical_device, swapchain, NULL);
    swapchain = 0;

    memset(swapchain_images.images, 0, sizeof swapchain_images.images);
    memset(
        swapchain_images.image_views, 0, sizeof swapchain_images.image_views);
    swapchain_images.count = SWAPCHAIN_MAX_IMAGES;
}

static inline GfxResult recreate_swapchain(void) {

    has_window_resized = 0;

    VK_ERR(vkDeviceWaitIdle(logical_device));
    destroy_swapchain();
    PROPAGATE(create_swapchain());

    destroy_depth_resources();
    PROPAGATE(create_depth_resources());

    return GFX_SUCCESS;
}

static inline GfxResult create_sync_objects(void) {

    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {

        VK_ERR(vkCreateSemaphore(logical_device,
                                 &semaphore_create_info,
                                 NULL,
                                 present_done_semaphores + i));
        VK_ERR(vkCreateSemaphore(logical_device,
                                 &semaphore_create_info,
                                 NULL,
                                 rendering_done_semaphores + i));

        VK_ERR(vkCreateFence(
            logical_device, &fence_create_info, NULL, draw_fences + i));
    }

    return GFX_SUCCESS;
}

GfxResult gapi_init(GfxInitInfo *info) {

    if (GapiObjectBuf_init(&objects) < 0)
        return GFX_SYSTEM_ERROR;
    if (GapiMeshBuf_init(&meshes) < 0)
        return GFX_SYSTEM_ERROR;
    if (GapiTextureBuf_init(&textures) < 0)
        return GFX_SYSTEM_ERROR;

    PROPAGATE(init_window(info->window.width,
                          info->window.height,
                          info->window.title,
                          info->window.flags));

    PROPAGATE(create_instance());
    VK_ERR(glfwCreateWindowSurface(instance, window, NULL, &surface));
    PROPAGATE(pick_physical_device());
    PROPAGATE(create_logical_device());
    PROPAGATE(create_swapchain());
    PROPAGATE(create_drawing_command_buffers());
    PROPAGATE(create_depth_resources());
    PROPAGATE(create_descriptor_set_layout());

    ERR((pipelines = calloc(info->shader_count, sizeof(VkPipeline))) == NULL,
        "calloc()");

    for (uint32_t i = 0; i < info->shader_count; i++) {
        PROPAGATE(create_graphics_pipeline(info->shaders[i], pipelines + i));
    }

    PROPAGATE(create_sync_objects());

    return GFX_SUCCESS;
}

GfxResult gapi_mesh_upload(Mesh *mesh, GapiMeshHandle *out_mesh_handle) {

    GapiMesh gpu_mesh = {.index_count = mesh->index_count};

    PROPAGATE(upload_data(mesh->vertices,
                          mesh->vertex_count * sizeof *mesh->vertices,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          &gpu_mesh.vertex_buffer,
                          &gpu_mesh.vertex_memory));
    PROPAGATE(upload_data(mesh->indices,
                          mesh->index_count * sizeof *mesh->indices,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          &gpu_mesh.index_buffer,
                          &gpu_mesh.index_memory));

    GapiMeshHandle handle = meshes.count;
    SYS_ERR(GapiMeshBuf_append(&meshes, &gpu_mesh));

    *out_mesh_handle = handle;
    return GFX_SUCCESS;
}

GfxResult gapi_texture_upload(uint32_t *pixels,
                              uint32_t width,
                              uint32_t height,
                              TextureHandle *out_texture_handle) {

    GapiTexture texture = {0};

    VkDeviceSize image_size = width * height * sizeof *pixels;
    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    create_buffer(image_size,
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                  &staging_buffer,
                  &staging_buffer_memory);

    // Fill staging buffer
    void *mapped_data;
    VK_ERR(vkMapMemory(
        logical_device, staging_buffer_memory, 0, image_size, 0, &mapped_data));
    memcpy(mapped_data, pixels, image_size);
    vkUnmapMemory(logical_device, staging_buffer_memory);

    // Create image

    PROPAGATE(create_image(width,
                           height,
                           VK_FORMAT_R8G8B8A8_SRGB,
                           VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           &texture.image,
                           &texture.image_memory));

    transition_image_layout(texture.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            0,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    copy_buffer_to_image(staging_buffer, texture.image, width, height);
    transition_image_layout(texture.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);

    VK_ERR(vkQueueWaitIdle(queue));
    destroy_buffer(staging_buffer, staging_buffer_memory);

    // Create image view

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VK_ERR(vkCreateImageView(
        logical_device, &image_view_create_info, NULL, &texture.image_view));

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
    int max_anisotropy = physical_device_properties.limits.maxSamplerAnisotropy;

    VkSamplerCreateInfo sampler_create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = max_anisotropy,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VK_ERR(vkCreateSampler(
        logical_device, &sampler_create_info, NULL, &texture.sampler));

    GapiTextureHandle handle = textures.count;
    SYS_ERR(GapiTextureBuf_append(&textures, &texture));

    *out_texture_handle = handle;
    return GFX_SUCCESS;
}

GfxResult gapi_object_create(GapiMeshHandle mesh_handle,
                             GapiTextureHandle texture_handle,
                             GapiObjectHandle *out_object_handle) {

    GapiObject new_object = {
        .mesh_handle = mesh_handle,
        .model_matrix = GLM_MAT4_IDENTITY_INIT,
    };
    GapiObjectHandle handle = objects.count;
    SYS_ERR(GapiObjectBuf_append(&objects, &new_object));

    GapiObject *object = objects.data + handle;

    PROPAGATE(create_descriptor_pool());

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        PROPAGATE(create_uniform_buffer(object->uniform_buffers + i,
                                        object->uniform_buffer_memories + i,
                                        object->uniform_buffer_mappings + i));
    }

    GapiTexture *texture = GapiTextureBuf_get(&textures, texture_handle);
    if (texture == NULL)
        return GFX_SYSTEM_ERROR;

    PROPAGATE(create_descriptor_sets(MAX_FRAMES_IN_FLIGHT,
                                     object->uniform_buffers,
                                     texture,
                                     object->descriptor_sets));

    *out_object_handle = handle;
    return GFX_SUCCESS;
}

void gapi_object_set_matrix(GapiObjectHandle object_handle,
                            mat4 *model_matrix) {
}

VkResult gapi_get_vulkan_error(void) {
    return vulkan_error;
}

GfxResult gapi_render_begin(void) {

    VK_ERR(vkQueueWaitIdle(queue));

    VkResult result =
        vkAcquireNextImageKHR(logical_device,
                              swapchain,
                              UINT64_MAX,
                              present_done_semaphores[frame_index],
                              NULL,
                              &image_index);

    switch (result) {
    case VK_SUCCESS:
        break;

    case VK_SUBOPTIMAL_KHR:
    case VK_ERROR_OUT_OF_DATE_KHR:
        PROPAGATE(recreate_swapchain());
        PROPAGATE(gapi_render_begin());
        return GFX_SUCCESS;

    default:
        VK_ERR(result);
        break;
    }

    VkCommandBuffer cmd_buf = drawing_command_buffers[frame_index];

    // Begin command buffer
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_ERR(vkBeginCommandBuffer(cmd_buf, &begin_info));

    transition_image_layout(depth_image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT);

    transition_swapchain_image_layout(
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    // Black
    VkClearValue clear_color = {.color = {.float32 = {0, 0, 0, 1}}};
    VkClearValue clear_depth = {.depthStencil = {1, 0}};

    VkRenderingAttachmentInfo color_att_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_images.image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear_color,
    };
    VkRenderingAttachmentInfo depth_att_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = clear_depth,
    };
    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = swap_extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_att_info,
        .pDepthAttachment = &depth_att_info,
    };

    vkCmdBeginRendering(cmd_buf, &rendering_info);

    //  TODO: Choose pipeline / shader index
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[0]);

    VkViewport viewport = {0, 0, swap_extent.width, swap_extent.height, 0, 1};
    vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
    VkRect2D scissor = {.extent = swap_extent};
    vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

    return GFX_SUCCESS;
}

GfxResult gapi_render_end(void) {

    VkCommandBuffer cmd_buf = drawing_command_buffers[frame_index];

    vkCmdEndRendering(cmd_buf);
    transition_swapchain_image_layout(
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(cmd_buf);

    VK_ERR(vkResetFences(logical_device, 1, &draw_fences[frame_index]));

    VkPipelineStageFlags wait_destination_stage_mask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = present_done_semaphores + frame_index,
        .pWaitDstStageMask = &wait_destination_stage_mask,
        .commandBufferCount = 1,
        .pCommandBuffers = drawing_command_buffers + frame_index,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = rendering_done_semaphores + frame_index,
    };
    VK_ERR(vkQueueSubmit(queue, 1, &submit_info, draw_fences[frame_index]));
    VK_ERR(vkWaitForFences(
        logical_device, 1, draw_fences + frame_index, VK_TRUE, UINT64_MAX));

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = rendering_done_semaphores + frame_index,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };

    frame_index = (frame_index + 1) % MAX_FRAMES_IN_FLIGHT;

    VkResult result = vkQueuePresentKHR(queue, &present_info);

    switch (result) {
    case VK_SUCCESS:
        break;

    case VK_SUBOPTIMAL_KHR:
    case VK_ERROR_OUT_OF_DATE_KHR:
        recreate_swapchain();
        break;

    default:
        VK_ERR(result);
    }

    if (has_window_resized)
        recreate_swapchain();

    return GFX_SUCCESS;
}

static inline void update_uniform_buffer(GapiObject *object) {

    UBOData ubo_data = {
        .view = GLM_MAT4_IDENTITY_INIT,
        .projection = GLM_MAT4_IDENTITY_INIT,
    };
    memcpy(ubo_data.model, object->model_matrix, sizeof(mat4));

    vec3 up = {0, 1, 0};
    vec3 right = {1, 0, 0};
    vec3 eye = {2, 2, 2};
    vec3 target = {0, 0, 0};
    float aspect_ratio = (float)swap_extent.width / swap_extent.height;

    glm_lookat(eye, target, up, ubo_data.view);
    glm_perspective(M_PI * 0.25, aspect_ratio, 0.1, 10, ubo_data.projection);
    ubo_data.projection[1][1] *= -1;

    memcpy(object->uniform_buffer_mappings[frame_index],
           &ubo_data,
           sizeof ubo_data);
}

void gapi_object_draw(GapiObjectHandle object_handle) {

    VkCommandBuffer cmd_buf = drawing_command_buffers[frame_index];
    GapiObject *object = objects.data + object_handle;
    GapiMesh *mesh = meshes.data + object->mesh_handle;

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd_buf, 0, 1, &mesh->vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmd_buf, mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(cmd_buf,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout,
                            0,
                            1,
                            object->descriptor_sets + frame_index,
                            0,
                            NULL);

    vkCmdDrawIndexed(cmd_buf, mesh->index_count, 1, 0, 0, 0);

    // Update uniform buffer for object
    update_uniform_buffer(object);
}

int gapi_window_should_close(void) {
    glfwPollEvents();
    return glfwWindowShouldClose(window);
}

const char *gapi_strerror(GfxResult result) {
    switch (result) {
    case GFX_SUCCESS:
        return "Success";
    case GFX_ERROR_GENERIC:
        return "Unknown error";
    case GFX_SYSTEM_ERROR:
        return "A system error occurred, check errno";
    case GFX_VULKAN_ERROR:
        return "A vulkan error occurred, check gapi_get_vulkan_error()";
    case GFX_GLFW_ERROR:
        return "A glfw error occurred";
    case GFX_NO_DEVICE_FOUND:
        return "No suitable physical device found";
    case GFX_VULKAN_FEATURE_UNSUPPORTED:
        return "A required Vulkan feature is not supported";
    }
}
