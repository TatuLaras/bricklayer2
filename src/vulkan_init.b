#include "vulkan_init.h"
#include "math_macros.h"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

static VkCommandPool command_pool = NULL;

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

GapiResult create_instance(VkInstance *out_instance) {
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

GapiResult create_logical_device(uint32_t queue_index,
                                 VkPhysicalDevice physical_device,
                                 VkDevice *out_device,
                                 VkQueue *out_queue) {
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

    const char *required_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    };
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = COUNT(required_extensions),
        .ppEnabledExtensionNames = required_extensions,
    };

    VK_ERR(
        vkCreateDevice(physical_device, &device_create_info, NULL, out_device));
    vkGetDeviceQueue(*out_device, queue_index, 0, out_queue);

    return GAPI_SUCCESS;
}

GapiResult create_swapchain(VkPhysicalDevice physical_device,
                            VkDevice device,
                            GLFWwindow *window,
                            VkSurfaceKHR surface,
                            VkExtent2D *out_swap_extent,
                            VkSurfaceFormatKHR *out_surface_format,
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
create_swapchain_image_views(VkDevice device,
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

static inline GapiResult
create_command_buffers(uint32_t queue_index,
                       VkDevice device,
                       uint32_t command_buffer_count,
                       VkCommandBuffer *out_command_buffers) {

    // Create command pool
    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_index,
    };
    VK_ERR(vkCreateCommandPool(
        device, &command_pool_create_info, NULL, &command_pool));

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = command_buffer_count,
    };
    VK_ERR(vkAllocateCommandBuffers(device, &alloc_info, out_command_buffers));

    return GAPI_SUCCESS;
}

GapiResult init_window(uint32_t width,
                       uint32_t height,
                       const char *title,
                       GapiWindowFlags flags,
                       GLFWframebuffersizefun window_resize_callback,
                       GLFWwindow *out_window) {

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    if (flags & GAPI_WINDOW_RESIZEABLE)
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    else
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    out_window = glfwCreateWindow(width, height, title, NULL, NULL);

    if (out_window == NULL) {
        const char *msg = 0;
        glfwGetError(&msg);
        ERROR("%s", msg);
        return GAPI_GLFW_ERROR;
    }
    glfwSetFramebufferSizeCallback(out_window, window_resize_callback);

    return GAPI_SUCCESS;
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

static inline GapiResult create_image(VkPhysicalDevice physical_device,
                                      VkDevice device,
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

static inline GapiResult
create_depth_resources(VkPhysicalDevice physical_device,
                       VkExtent2D swap_extent,
                       VkFormat *out_depth_format) {

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

    PROPAGATE(create_image(swap_extent.width,
                           swap_extent.height,
                           *out_depth_format,
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

    return GAPI_SUCCESS;
}

//  TODO: Depends on the shader used, dynamically create?
static inline GapiResult create_descriptor_set_layout(void) {

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
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
        .bindingCount = COUNT(layout_bindings),
        .pBindings = layout_bindings,
    };
    VK_ERR(vkCreateDescriptorSetLayout(
        logical_device, &layout_create_info, NULL, &descriptor_set_layout));

    return GAPI_SUCCESS;
}

static inline void push_descriptor_set(GapiTexture *texture,
                                       VkBuffer *uniform_buffers) {

    VkDescriptorBufferInfo buf_info = {
        .buffer = uniform_buffers[frame_index],
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

    VkCommandBuffer cmd_buf = drawing_command_buffers[frame_index];
    vkCmdPushDescriptorSet(cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline_layout,
                           0,
                           COUNT(descriptor_writes),
                           descriptor_writes);
}

static inline GapiResult create_graphics_pipeline(GapiShader shader,
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

    return GAPI_SUCCESS;
}

static inline GapiResult
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

    return GAPI_SUCCESS;
}

static inline GapiResult
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

    return GAPI_SUCCESS;
}

static inline GapiResult
end_single_time_commands(VkCommandBuffer command_buffer) {

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

static inline GapiResult upload_data(void *data,
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

    return GAPI_SUCCESS;
}

static inline GapiResult create_uniform_buffer(VkBuffer *out_buffer,
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

    return GAPI_SUCCESS;
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

static inline GapiResult recreate_swapchain(void) {

    has_window_resized = 0;

    VK_ERR(vkDeviceWaitIdle(logical_device));
    destroy_swapchain();
    PROPAGATE(create_swapchain());

    destroy_depth_resources();
    PROPAGATE(create_depth_resources());

    return GAPI_SUCCESS;
}

static inline GapiResult create_sync_objects(void) {

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

    return GAPI_SUCCESS;
}
