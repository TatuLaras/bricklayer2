#include "gapi_low_level.h"

#include "utility_macros.h"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

static inline GapiResult
find_supported_depth_format(VkPhysicalDevice physical_device,
                            uint32_t candidates_count,
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
            return GAPI_SUCCESS;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (properties.optimalTilingFeatures & flags) == flags) {
            *out_format = candidates[i];
            return GAPI_SUCCESS;
        }
    }

    return GAPI_VULKAN_FEATURE_UNSUPPORTED;
}

static inline GapiResult find_graphics_present_queue(
    VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t *out_queue_index) {

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
        return GAPI_SUCCESS;
    }

    return GAPI_ERROR_GENERIC;
}

static inline GapiResult
pick_physical_device(VkInstance instance,
                     VkSurfaceKHR surface,
                     VkPhysicalDevice *out_physical_device,
                     uint32_t *out_queue_index) {

    uint32_t device_count = 0;
    VK_ERR(vkEnumeratePhysicalDevices(instance, &device_count, NULL));

    VkPhysicalDevice devices[device_count];
    VK_ERR(vkEnumeratePhysicalDevices(instance, &device_count, devices));

    //  FIXME: Proper checking for device capabilities

    for (uint32_t i = 0; i < device_count; i++) {

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(devices[i], &device_properties);

        if (find_graphics_present_queue(devices[i], surface, out_queue_index) !=
            GAPI_SUCCESS)
            continue;

        *out_physical_device = devices[i];
        return GAPI_SUCCESS;
    }

    ERROR("no suitable GPU found");
    return GAPI_NO_DEVICE_FOUND;
}

static inline GapiResult find_memory_type(VkPhysicalDevice physical_device,
                                          VkMemoryRequirements requirements,
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
        return GAPI_VULKAN_FEATURE_UNSUPPORTED;

    *out_memory_type = selected_memory_type;
    return GAPI_SUCCESS;
}

static inline GapiResult
begin_single_time_commands(VkDevice device,
                           VkCommandPool command_pool,
                           VkCommandBuffer *command_buffer) {

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_ERR(vkAllocateCommandBuffers(device, &alloc_info, command_buffer));

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_ERR(vkBeginCommandBuffer(*command_buffer, &begin_info));

    return GAPI_SUCCESS;
}

static inline GapiResult
end_single_time_commands(VkCommandBuffer command_buffer, VkQueue queue) {

    VK_ERR(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    VK_ERR(vkQueueSubmit(queue, 1, &submit_info, NULL));
    VK_ERR(vkQueueWaitIdle(queue));

    return GAPI_SUCCESS;
}

static inline void buffer_copy(VkDevice device,
                               VkCommandPool command_pool,
                               VkBuffer dst,
                               VkBuffer src,
                               VkDeviceSize size,
                               VkQueue queue) {

    VkCommandBuffer command_buffer;
    begin_single_time_commands(device, command_pool, &command_buffer);

    // Copy command
    VkBufferCopy regions = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size,
    };
    vkCmdCopyBuffer(command_buffer, src, dst, 1, &regions);

    end_single_time_commands(command_buffer, queue);
}

static inline void copy_buffer_to_image(VkDevice device,
                                        VkCommandPool command_pool,
                                        VkQueue queue,
                                        VkBuffer buffer,
                                        VkImage image,
                                        uint32_t width,
                                        uint32_t height) {

    VkCommandBuffer cmd_buf;
    begin_single_time_commands(device, command_pool, &cmd_buf);

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

    end_single_time_commands(cmd_buf, queue);
}

GapiResult gll_instance_create(VkInstance *out_instance) {
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
        return GAPI_VULKAN_FEATURE_UNSUPPORTED;
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
        return GAPI_GLFW_ERROR;
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
            return GAPI_VULKAN_FEATURE_UNSUPPORTED;
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
    VK_ERR(vkCreateInstance(&create_info, NULL, out_instance));
    free(required_extensions);

    return GAPI_SUCCESS;
}

GapiResult gll_device_create(VkInstance instance,
                             VkSurfaceKHR surface,
                             VkPhysicalDevice *out_physical_device,
                             VkDevice *out_device,
                             uint32_t *out_queue_index,
                             VkQueue *out_queue) {

    pick_physical_device(
        instance, surface, out_physical_device, out_queue_index);

    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR latest = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR,
        .presentModeFifoLatestReady = VK_TRUE,
    };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT f1 = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .pNext = &latest,
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
        .queueFamilyIndex = *out_queue_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char *required_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
        VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME,
    };
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = COUNT(required_extensions),
        .ppEnabledExtensionNames = required_extensions,
    };

    VK_ERR(vkCreateDevice(
        *out_physical_device, &device_create_info, NULL, out_device));
    vkGetDeviceQueue(*out_device, *out_queue_index, 0, out_queue);

    return GAPI_SUCCESS;
}

GapiResult gll_swapchain_create(VkDevice device,
                                VkPhysicalDevice physical_device,
                                GLFWwindow *window,
                                VkSurfaceKHR surface,
                                VkSurfaceFormatKHR *out_surface_format,
                                VkExtent2D *out_swap_extent,
                                VkSwapchainKHR *out_swapchain) {

    VkSurfaceCapabilitiesKHR surface_capabilities;
    VK_ERR(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical_device, surface, &surface_capabilities));

    // Choose swap extent
    *out_swap_extent = surface_capabilities.currentExtent;
    if (out_swap_extent->width == 0xffffffff) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        out_swap_extent->width =
            CLAMP((uint32_t)width,
                  surface_capabilities.minImageExtent.width,
                  surface_capabilities.maxImageExtent.width);
        out_swap_extent->height =
            CLAMP((uint32_t)height,
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

        if (present_modes[i] == VK_PRESENT_MODE_FIFO_LATEST_READY_KHR) {
            present_mode = present_modes[i];
            break;
        }
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = present_modes[i];
            break;
        }
        if (present_modes[i] == VK_PRESENT_MODE_FIFO_KHR)
            present_mode = present_modes[i];
    }

    if (present_mode == VK_PRESENT_MODE_MAX_ENUM_KHR) {
        ERROR("no suitable present mode found");
        return GAPI_VULKAN_FEATURE_UNSUPPORTED;
    }

    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = min_image_count,
        .imageFormat = out_surface_format->format,
        .imageColorSpace = out_surface_format->colorSpace,
        .imageExtent = *out_swap_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
    };
    VK_ERR(vkCreateSwapchainKHR(
        device, &swapchain_create_info, NULL, out_swapchain));

    return GAPI_SUCCESS;
}

GapiResult
gll_swapchain_image_views_create(VkDevice device,
                                 VkSwapchainKHR swapchain,
                                 VkSurfaceFormatKHR surface_format,
                                 uint32_t *swapchain_image_count,
                                 VkImage *out_swapchain_images,
                                 VkImageView *out_swapchain_image_views) {

    VK_ERR(vkGetSwapchainImagesKHR(
        device, swapchain, swapchain_image_count, out_swapchain_images));

    // Create image views
    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = surface_format.format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    for (uint32_t i = 0; i < *swapchain_image_count; i++) {
        image_view_create_info.image = out_swapchain_images[i];
        VK_ERR(vkCreateImageView(device,
                                 &image_view_create_info,
                                 NULL,
                                 out_swapchain_image_views + i));
    }

    return GAPI_SUCCESS;
}

GapiResult gll_command_pool_create(VkDevice device,
                                   uint32_t queue_index,
                                   VkCommandPool *out_command_pool) {

    // Create command pool
    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_index,
    };
    VK_ERR(vkCreateCommandPool(
        device, &command_pool_create_info, NULL, out_command_pool));

    return GAPI_SUCCESS;
}

GapiResult gll_command_buffers_create(VkDevice device,
                                      VkCommandPool command_pool,
                                      uint32_t count,
                                      VkCommandBuffer *out_command_buffers) {

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = count,
    };
    VK_ERR(vkAllocateCommandBuffers(device, &alloc_info, out_command_buffers));

    return GAPI_SUCCESS;
}

GapiResult gll_window_init(uint32_t width,
                           uint32_t height,
                           const char *title,
                           GapiWindowFlags flags,
                           GLFWframebuffersizefun window_resize_callback,
                           GLFWwindow **out_window) {

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    if (flags & GAPI_WINDOW_RESIZEABLE)
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    else
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    *out_window = glfwCreateWindow(width, height, title, NULL, NULL);

    if (*out_window == NULL) {
        const char *msg = 0;
        glfwGetError(&msg);
        ERROR("%s", msg);
        return GAPI_GLFW_ERROR;
    }

    if (window_resize_callback)
        glfwSetFramebufferSizeCallback(*out_window, window_resize_callback);

    return GAPI_SUCCESS;
}

GapiResult gll_image_create(VkDevice device,
                            VkPhysicalDevice physical_device,
                            uint32_t width,
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
    VK_ERR(vkCreateImage(device, &image_create_info, NULL, out_image));

    // Create memory
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device, *out_image, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
    };
    PROPAGATE(find_memory_type(physical_device,
                               memory_requirements,
                               properties,
                               &alloc_info.memoryTypeIndex));
    VK_ERR(vkAllocateMemory(device, &alloc_info, NULL, out_memory));
    VK_ERR(vkBindImageMemory(device, *out_image, *out_memory, 0));

    return GAPI_SUCCESS;
}

GapiResult gll_depth_resources_create(VkDevice device,
                                      VkPhysicalDevice physical_device,
                                      VkExtent2D swap_extent,
                                      VkFormat *out_depth_format,
                                      VkImage *out_depth_image,
                                      VkDeviceMemory *out_depth_image_memory,
                                      VkImageView *out_depth_image_view) {

    VkFormat format_candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    PROPAGATE(find_supported_depth_format(
        physical_device,
        COUNT(format_candidates),
        format_candidates,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
        out_depth_format));

    PROPAGATE(gll_image_create(device,
                               physical_device,
                               swap_extent.width,
                               swap_extent.height,
                               *out_depth_format,
                               VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               out_depth_image,
                               out_depth_image_memory));

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *out_depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = *out_depth_format,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    };
    VK_ERR(vkCreateImageView(
        device, &image_view_create_info, NULL, out_depth_image_view));

    return GAPI_SUCCESS;
}

//  TODO: Depends on the shader used, dynamically create?
GapiResult gll_descritor_set_layout_create(
    VkDevice device,
    uint32_t bindings_count,
    VkDescriptorSetLayoutBinding *bindings,
    VkDescriptorSetLayout *out_descriptor_set_layout) {

    VkDescriptorSetLayoutCreateInfo layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
        .bindingCount = bindings_count,
        .pBindings = bindings,
    };
    VK_ERR(vkCreateDescriptorSetLayout(
        device, &layout_create_info, NULL, out_descriptor_set_layout));

    return GAPI_SUCCESS;
}

GapiResult gll_pipeline_create(VkDevice device,
                               VkSurfaceFormatKHR surface_format,
                               VkShaderModule shader_module,
                               VkDescriptorSetLayout descriptor_set_layout,
                               VkFormat depth_format,
                               GapiPipelineCreateInfo *create_info,
                               VkPipelineLayout *out_pipeline_layout,
                               VkPipeline *out_pipeline) {

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

    VkPrimitiveTopology topology;
    switch (create_info->topology) {
    case GAPI_TOPOLOGY_TRIANGLES:
        topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    case GAPI_TOPOLOGY_LINES:
        topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    case GAPI_TOPOLOGY_POINTS:
        topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        break;
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
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
        // .cullMode = VK_CULL_MODE_NONE,
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

    VkPipelineColorBlendAttachmentState color_blend_attachment;
    switch (create_info->alpha_blending_mode) {

    case GAPI_ALPHA_BLENDING_NONE:
        color_blend_attachment = (VkPipelineColorBlendAttachmentState){
            .blendEnable = VK_FALSE,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        break;
    case GAPI_ALPHA_BLENDING_BLEND:
        color_blend_attachment = (VkPipelineColorBlendAttachmentState){
            .blendEnable = VK_TRUE,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
        };
        break;
    case GAPI_ALPHA_BLENDING_ADDITIVE:
        color_blend_attachment = (VkPipelineColorBlendAttachmentState){
            .blendEnable = VK_TRUE,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
        };
        break;
    }

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
        device, &layout_create_info, NULL, out_pipeline_layout));

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
        .layout = *out_pipeline_layout,
        .renderPass = NULL,
    };
    VK_ERR(vkCreateGraphicsPipelines(
        device, NULL, 1, &graphics, NULL, out_pipeline));

    return GAPI_SUCCESS;
}

GapiResult gll_buffer_create(VkDevice device,
                             VkPhysicalDevice physical_device,
                             VkDeviceSize size,
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
    VK_ERR(vkCreateBuffer(device, &buffer_create_info, NULL, out_buffer));

    // Get memory requirements and determine memory type
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device, *out_buffer, &memory_requirements);

    uint32_t selected_memory_type;
    PROPAGATE(find_memory_type(physical_device,
                               memory_requirements,
                               memory_property_flags,
                               &selected_memory_type));

    // Allocate and bind
    VkMemoryAllocateInfo memory_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = selected_memory_type,
    };
    VK_ERR(vkAllocateMemory(device, &memory_allocate_info, NULL, out_memory));

    VK_ERR(vkBindBufferMemory(device, *out_buffer, *out_memory, 0));

    return GAPI_SUCCESS;
}

void gll_buffer_destroy(VkDevice device,
                        VkBuffer buffer,
                        VkDeviceMemory memory) {
    vkFreeMemory(device, memory, NULL);
    vkDestroyBuffer(device, buffer, NULL);
}

GapiResult gll_upload_data(VkDevice device,
                           VkPhysicalDevice physical_device,
                           VkCommandPool command_pool,
                           VkQueue queue,
                           void *data,
                           uint32_t size,
                           VkBufferUsageFlagBits usage,
                           VkBuffer *out_buffer,
                           VkDeviceMemory *out_memory) {

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    PROPAGATE(gll_buffer_create(device,
                                physical_device,
                                size,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                &staging_buffer,
                                &staging_buffer_memory));

    PROPAGATE(gll_buffer_create(device,
                                physical_device,
                                size,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                out_buffer,
                                out_memory));

    // Fill staging buffer
    void *mapped_data;
    VK_ERR(
        vkMapMemory(device, staging_buffer_memory, 0, size, 0, &mapped_data));
    memcpy(mapped_data, data, size);
    vkUnmapMemory(device, staging_buffer_memory);

    // Copy from staging buffer to actual buffer
    buffer_copy(device, command_pool, *out_buffer, staging_buffer, size, queue);

    VK_ERR(vkQueueWaitIdle(queue));
    gll_buffer_destroy(device, staging_buffer, staging_buffer_memory);

    return GAPI_SUCCESS;
}

GapiResult gll_uniform_buffer_create(VkDevice device,
                                     VkPhysicalDevice physical_device,
                                     VkDeviceSize size,
                                     VkBuffer *out_buffer,
                                     VkDeviceMemory *out_memory,
                                     void **out_mapping) {

    PROPAGATE(gll_buffer_create(device,
                                physical_device,
                                size,
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                out_buffer,
                                out_memory));

    VK_ERR(vkMapMemory(device, *out_memory, 0, size, 0, out_mapping));

    return GAPI_SUCCESS;
}

void gll_transition_swapchain_image_layout(
    VkCommandBuffer command_buffer,
    VkImage image,
    VkImageLayout old_layout,
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
        .image = image,
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

    vkCmdPipelineBarrier2(command_buffer, &dependency_info);
}

void gll_transition_image_layout(VkDevice device,
                                 VkCommandPool command_pool,
                                 VkQueue queue,
                                 VkImage image,
                                 VkImageLayout old_layout,
                                 VkImageLayout new_layout,
                                 VkAccessFlags src_access_mask,
                                 VkAccessFlags dst_access_mask,
                                 VkPipelineStageFlags src_stage,
                                 VkPipelineStageFlags dst_stage,
                                 VkImageAspectFlags image_aspect_flags) {

    VkCommandBuffer cmd_buf;
    begin_single_time_commands(device, command_pool, &cmd_buf);

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

    end_single_time_commands(cmd_buf, queue);
}

void gll_image_resources_destroy(VkDevice device,
                                 VkImage image,
                                 VkDeviceMemory memory,
                                 VkImageView image_view) {

    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyImageView(device, image_view, NULL);
}

void gll_push_descriptor_set(VkCommandBuffer command_buffer,
                             VkPipelineLayout pipeline_layout,
                             GapiTexture *texture,
                             VkBuffer uniform_buffer) {

    VkDescriptorBufferInfo buf_info = {
        .buffer = uniform_buffer,
        .offset = 0,
        .range = sizeof(GapiUBO),
    };

    VkDescriptorImageInfo image_info = {
        .sampler = texture->sampler,
        .imageView = texture->image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet descriptor_writes[] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buf_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
        },
    };

    vkCmdPushDescriptorSet(command_buffer,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_layout,
                           0,
                           COUNT(descriptor_writes),
                           descriptor_writes);
}

GapiResult gll_semaphores_create(VkDevice device,
                                 uint32_t count,
                                 VkSemaphore *out_semaphores) {

    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    for (uint32_t i = 0; i < count; i++) {
        VK_ERR(vkCreateSemaphore(
            device, &semaphore_create_info, NULL, out_semaphores + i));
    }

    return GAPI_SUCCESS;
}

GapiResult
gll_fences_create(VkDevice device, uint32_t count, VkFence *out_fences) {

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

    for (uint32_t i = 0; i < count; i++) {
        VK_ERR(vkCreateFence(device, &fence_create_info, NULL, out_fences + i));
    }

    return GAPI_SUCCESS;
}

GapiResult gll_texture_create(VkDevice device,
                              VkCommandPool command_pool,
                              VkPhysicalDevice physical_device,
                              VkQueue queue,
                              uint32_t *pixels,
                              uint32_t width,
                              uint32_t height,
                              GapiTexture *out_texture) {

    GapiTexture texture = {0};

    VkDeviceSize image_size = width * height * sizeof *pixels;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    gll_buffer_create(device,
                      physical_device,
                      image_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                      &staging_buffer,
                      &staging_buffer_memory);

    // Fill staging buffer
    void *mapped_data;
    VK_ERR(vkMapMemory(
        device, staging_buffer_memory, 0, image_size, 0, &mapped_data));
    memcpy(mapped_data, pixels, image_size);
    vkUnmapMemory(device, staging_buffer_memory);

    // Create image

    PROPAGATE(gll_image_create(device,
                               physical_device,
                               width,
                               height,
                               VK_FORMAT_R8G8B8A8_SRGB,
                               VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_SAMPLED_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               &texture.image,
                               &texture.image_memory));

    gll_transition_image_layout(device,
                                command_pool,
                                queue,
                                texture.image,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                0,
                                VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT);
    copy_buffer_to_image(device,
                         command_pool,
                         queue,
                         staging_buffer,
                         texture.image,
                         width,
                         height);
    gll_transition_image_layout(device,
                                command_pool,
                                queue,
                                texture.image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT);

    VK_ERR(vkQueueWaitIdle(queue));
    gll_buffer_destroy(device, staging_buffer, staging_buffer_memory);

    // Create image view

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VK_ERR(vkCreateImageView(
        device, &image_view_create_info, NULL, &texture.image_view));

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

    VK_ERR(
        vkCreateSampler(device, &sampler_create_info, NULL, &texture.sampler));

    *out_texture = texture;
    return GAPI_SUCCESS;
}

void gll_texture_destroy(VkDevice device, GapiTexture *texture) {

    if (texture->sampler != NULL) {
        vkDestroySampler(device, texture->sampler, NULL);
        texture->sampler = NULL;
    }
    if (texture->image_view != NULL) {
        vkDestroyImageView(device, texture->image_view, NULL);
        texture->image_view = NULL;
    }
    if (texture->image != NULL) {
        vkDestroyImage(device, texture->image, NULL);
        texture->image = NULL;
    }
    if (texture->image_memory != NULL) {
        vkFreeMemory(device, texture->image_memory, NULL);
        texture->image_memory = NULL;
    }
}

void gll_mesh_destroy(VkDevice device, GapiMesh *mesh) {

    if (mesh->vertex_buffer != NULL) {
        vkDestroyBuffer(device, mesh->vertex_buffer, NULL);
        mesh->vertex_buffer = NULL;
    }
    if (mesh->index_buffer != NULL) {
        vkDestroyBuffer(device, mesh->index_buffer, NULL);
        mesh->index_buffer = NULL;
    }
    if (mesh->vertex_memory != NULL) {
        vkFreeMemory(device, mesh->vertex_memory, NULL);
        mesh->vertex_memory = NULL;
    }
    if (mesh->index_memory != NULL) {
        vkFreeMemory(device, mesh->index_memory, NULL);
        mesh->index_memory = NULL;
    }
}
