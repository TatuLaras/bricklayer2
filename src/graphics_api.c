#include "graphics_api.h"

#include "cglm/types.h"
#include "log.h"
#include "math_macros.h"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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

#define MAX_FRAMES_IN_FLIGHT 2
#define SWAPCHAIN_MAX_IMAGES 16

// typedef struct {
//     mat4 model;
//     mat4 view;
//     mat4 projection;
// } UBOData;

static GLFWwindow *window = NULL;
static int has_window_resized = 0;

static VkInstance instance = NULL;
static VkSurfaceKHR surface = NULL;

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

static VkCommandPool command_pool = NULL;
static VkCommandBuffer drawing_command_buffers[MAX_FRAMES_IN_FLIGHT] = {0};

static VkImage depth_image = NULL;
static VkDeviceMemory depth_image_memory;
static VkImageView depth_image_view;

// static VkBuffer uniform_buffers[MAX_FRAMES_IN_FLIGHT] = {0};
// static VkDeviceMemory uniform_buffer_memories[MAX_FRAMES_IN_FLIGHT] = {0};
// static void *uniform_buffer_mappings[MAX_FRAMES_IN_FLIGHT] = {0};
// static VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT] = {0};

static VkDescriptorPool descriptor_pool = NULL;
static VkDescriptorSetLayout descriptor_set_layout = NULL;

static VkPipeline *pipelines = NULL;
static VkPipelineLayout pipeline_layout = NULL;

static void
window_resize_callback(GLFWwindow *_window, int _width, int _height) {
    (void)_window;
    (void)_width;
    (void)_height;

    has_window_resized = 1;
}

static inline void init_window(uint32_t width,
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
        exit(EXIT_FAILURE);
    }
    glfwSetFramebufferSizeCallback(window, window_resize_callback);
}

static inline void create_instance(void) {
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
        exit(EXIT_FAILURE);
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
            exit(EXIT_FAILURE);
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
}

static inline int find_queue(VkPhysicalDevice device,
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
        return 0;
    }

    return -1;
}

static inline void pick_physical_device(void) {

    uint32_t device_count = 0;
    VK_ERR(vkEnumeratePhysicalDevices(instance, &device_count, NULL));

    VkPhysicalDevice devices[device_count];
    VK_ERR(vkEnumeratePhysicalDevices(instance, &device_count, devices));

    //  FIXME: Proper checking for device capabilities

    for (uint32_t i = 0; i < device_count; i++) {

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(devices[i], &device_properties);

        if (find_queue(devices[i], &queue_index) < 0)
            continue;

        physical_device = devices[i];
        return;
    }

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
}

static inline void create_swapchain(VkSurfaceFormatKHR *out_surface_format) {

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

    *out_surface_format = *formats;
    for (uint32_t i = 0; i < format_count; i++) {
        if (formats[i].format != VK_FORMAT_B8G8R8A8_SRGB)
            continue;
        if (formats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            continue;
        *out_surface_format = formats[i];
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
        exit(EXIT_FAILURE);
    }

    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = min_image_count,
        .imageFormat = out_surface_format->format,
        .imageColorSpace = out_surface_format->colorSpace,
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
        .format = out_surface_format->format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    for (uint32_t i = 0; i < swapchain_images.count; i++) {
        image_view_create_info.image = swapchain_images.images[i];
        VK_ERR(vkCreateImageView(logical_device,
                                 &image_view_create_info,
                                 NULL,
                                 swapchain_images.image_views + i));
    }
}

static inline void create_drawing_command_buffers(void) {

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
}

static inline uint32_t find_memory_type(VkMemoryRequirements requirements,
                                        VkMemoryPropertyFlags flags) {

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

    if (selected_memory_type < 0) {
        ERROR("no suitable memory type found for buffer");
        exit(EXIT_FAILURE);
    }

    return selected_memory_type;
}

static inline void create_image(uint32_t width,
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
        .memoryTypeIndex = find_memory_type(memory_requirements, properties),
    };
    VK_ERR(vkAllocateMemory(logical_device, &alloc_info, NULL, out_memory));
    VK_ERR(vkBindImageMemory(logical_device, *out_image, *out_memory, 0));
}

static inline VkFormat find_supported_format(uint32_t candidates_count,
                                             VkFormat *candidates,
                                             VkImageTiling tiling,
                                             VkFormatFeatureFlags flags) {

    for (uint32_t i = 0; i < candidates_count; i++) {

        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(
            physical_device, candidates[i], &properties);

        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (properties.linearTilingFeatures & flags) == flags)
            return candidates[i];
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (properties.optimalTilingFeatures & flags) == flags)
            return candidates[i];
    }

    ERROR("no suitable format found for depth buffer");
    exit(EXIT_FAILURE);
}

static inline void create_depth_resources(VkFormat *out_depth_format) {

    VkFormat format_candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    *out_depth_format =
        find_supported_format(COUNT(format_candidates),
                              format_candidates,
                              VK_IMAGE_TILING_OPTIMAL,
                              VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    create_image(swap_extent.width,
                 swap_extent.height,
                 *out_depth_format,
                 VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 &depth_image,
                 &depth_image_memory);

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = *out_depth_format,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    };
    VK_ERR(vkCreateImageView(
        logical_device, &image_view_create_info, NULL, &depth_image_view));
}

static inline void create_descriptor_set_layout(void) {

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
}

// static inline void create_uniform_buffers(void) {
//
//     memset(uniform_buffers, 0, sizeof uniform_buffers);
//     memset(uniform_buffer_memories, 0, sizeof uniform_buffer_memories);
//     memset(uniform_buffer_mappings, 0, sizeof uniform_buffer_mappings);
//
//     for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
//
//         VkDeviceSize size = sizeof(UBOData);
//         create_buffer(size,
//                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
//                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
//                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//                       uniform_buffers + i,
//                       uniform_buffer_memories + i);
//
//         VK_ERR(vkMapMemory(logical_device,
//                            uniform_buffer_memories[i],
//                            0,
//                            size,
//                            0,
//                            uniform_buffer_mappings + i));
//     }
// }

static inline void create_descriptor_pool(void) {

    // Create descriptor pool
    VkDescriptorPoolSize pool_sizes[] = {
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = MAX_FRAMES_IN_FLIGHT},
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = MAX_FRAMES_IN_FLIGHT},
    };
    VkDescriptorPoolCreateInfo pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        // .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes,
    };
    VK_ERR(vkCreateDescriptorPool(
        logical_device, &pool_create_info, NULL, &descriptor_pool));
}

// static inline void create_descriptor_sets(void) {
//
//     VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT] = {0};
//
//     for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
//         layouts[i] = descriptor_set_layout;
//
//     VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
//         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
//         .descriptorPool = descriptor_pool,
//         .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
//         .pSetLayouts = layouts,
//     };
//     VK_ERR(vkAllocateDescriptorSets(
//         logical_device, &descriptor_set_alloc_info, descriptor_sets));
//
//     for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
//
//         VkDescriptorBufferInfo buf_info = {
//             .buffer = uniform_buffers[i],
//             .offset = 0,
//             .range = sizeof(UBOData),
//         };
//         VkDescriptorImageInfo image_info = {
//             .sampler = sampler,
//             .imageView = texture_image_view,
//             .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
//         };
//         VkWriteDescriptorSet descriptor_writes[] = {
//             {
//                 .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
//                 .dstSet = descriptor_sets[i],
//                 .dstBinding = 0,
//                 .dstArrayElement = 0,
//                 .descriptorCount = 1,
//                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//                 .pBufferInfo = &buf_info,
//             },
//             {
//                 .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
//                 .dstSet = descriptor_sets[i],
//                 .dstBinding = 1,
//                 .dstArrayElement = 0,
//                 .descriptorCount = 1,
//                 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//                 .pImageInfo = &image_info,
//             },
//         };
//         vkUpdateDescriptorSets(logical_device, 2, descriptor_writes, 0,
//         NULL);
//     }
// }

static inline void create_graphics_pipeline(VkSurfaceFormatKHR surface_format,
                                            VkFormat depth_format,
                                            GfxShader shader,
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
}

void gapi_init(GfxInitInfo *info) {

    init_window(info->window.width,
                info->window.height,
                info->window.title,
                info->window.flags);

    create_instance();
    VK_ERR(glfwCreateWindowSurface(instance, window, NULL, &surface));

    pick_physical_device();

    create_logical_device();

    VkSurfaceFormatKHR surface_format;
    create_swapchain(&surface_format);

    create_drawing_command_buffers();

    VkFormat depth_format;
    create_depth_resources(&depth_format);

    create_descriptor_set_layout();
    create_descriptor_pool();

    // create_uniform_buffers();
    // create_descriptor_sets();

    ERR((pipelines = calloc(info->shader_count, sizeof(VkPipeline))) == NULL,
        "calloc()");

    for (uint32_t i = 0; i < info->shader_count; i++) {
        create_graphics_pipeline(
            surface_format, depth_format, info->shaders[i], pipelines + i);
    }
}
