#include "cglm/cam.h"
#include "cglm/cglm.h"
#include "log.h"
#include "math_macros.h"

#include <stddef.h>
#include <stdlib.h>
#include <time.h>
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

#define SWAPCHAIN_MAX_IMAGES 16
#define MAX_FRAMES_IN_FLIGHT 2

typedef struct {
    vec2 pos;
    vec3 color;
} VertexData;

typedef struct {
    mat4 model;
    mat4 view;
    mat4 projection;
} UniformData;

static GLFWwindow *window = 0;
static VkInstance instance = 0;
static VkSurfaceKHR surface = 0;
static VkPhysicalDevice physical_device = 0;
static VkDevice logical_device = 0;
static uint32_t queue_index = 0;
static VkQueue queue = 0;
static VkExtent2D swap_extent = {0};
static VkSwapchainKHR swapchain = 0;
static VkSurfaceFormatKHR surface_format = {0};
static VkPipeline pipeline = 0;
static VkPipelineLayout pipeline_layout = 0;

static VkCommandPool command_pool;
static uint32_t frame_index = 0;
static VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT] = {0};
static VkSemaphore present_done_semaphores[MAX_FRAMES_IN_FLIGHT] = {0};
static VkSemaphore rendering_done_semaphores[MAX_FRAMES_IN_FLIGHT] = {0};
static VkFence draw_fences[MAX_FRAMES_IN_FLIGHT] = {0};

static VkBuffer uniform_buffers[MAX_FRAMES_IN_FLIGHT] = {0};
static VkDeviceMemory uniform_buffer_memories[MAX_FRAMES_IN_FLIGHT] = {0};
static void *uniform_buffer_mappings[MAX_FRAMES_IN_FLIGHT] = {0};
static VkDescriptorPool descriptor_pool = 0;
static VkDescriptorSetLayout descriptor_set_layout = 0;
static VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT] = {0};

static struct {
    uint32_t count;
    VkImage images[SWAPCHAIN_MAX_IMAGES];
    VkImageView image_views[SWAPCHAIN_MAX_IMAGES];
} swapchain_images = {.count = SWAPCHAIN_MAX_IMAGES};

static VertexData vertices[] = {{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                                {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
                                {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
                                {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}};
static uint32_t indices[] = {0, 1, 2, 2, 3, 0};

static VkBuffer vertex_buffer = 0;
static VkBuffer index_buffer = 0;

const char shader[] = {
#embed "../build/shaders/shader.spv"
};

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

static inline void create_swap_chain(void) {

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
        exit(EXIT_FAILURE);
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
}

static inline void create_descriptor_set_layout(void) {

    VkDescriptorSetLayoutBinding layout_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &layout_binding,
    };
    VK_ERR(vkCreateDescriptorSetLayout(
        logical_device, &layout_create_info, NULL, &descriptor_set_layout));
}

static inline void create_graphics_pipeline(void) {

    VkShaderModule shader_module;
    VkShaderModuleCreateInfo shader_module_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof shader,
        .pCode = (uint32_t *)shader,
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
        .stride = sizeof(VertexData),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attribute_descriptions[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(VertexData, pos),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(VertexData, color),
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
        // .cullMode = VK_CULL_MODE_NONE,
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
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout,
        .renderPass = NULL,
    };
    VK_ERR(vkCreateGraphicsPipelines(
        logical_device, NULL, 1, &graphics, NULL, &pipeline));

    vkDestroyShaderModule(logical_device, shader_module, NULL);
}

static inline void create_buffer(VkDeviceSize size,
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

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    int selected_memory_type = -1;

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((memory_requirements.memoryTypeBits & (1 << i)) == 0)
            continue;
        if ((memory_properties.memoryTypes[i].propertyFlags &
             memory_property_flags) != memory_property_flags)
            continue;

        selected_memory_type = i;
        break;
    }

    if (selected_memory_type < 0) {
        ERROR("no suitable memory type found for buffer");
        exit(EXIT_FAILURE);
    }

    // Allocate and bind
    VkMemoryAllocateInfo memory_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = selected_memory_type,
    };
    VK_ERR(vkAllocateMemory(
        logical_device, &memory_allocate_info, NULL, out_memory));

    VK_ERR(vkBindBufferMemory(logical_device, *out_buffer, *out_memory, 0));
}

static inline void buffer_copy(VkBuffer dst, VkBuffer src, VkDeviceSize size) {

    // Make temporary command buffer
    VkCommandBuffer copy_command_buffer;
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_ERR(vkAllocateCommandBuffers(
        logical_device, &alloc_info, &copy_command_buffer));

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_ERR(vkBeginCommandBuffer(copy_command_buffer, &begin_info));

    // Copy command
    VkBufferCopy regions = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size,
    };
    vkCmdCopyBuffer(copy_command_buffer, src, dst, 1, &regions);

    VK_ERR(vkEndCommandBuffer(copy_command_buffer));

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &copy_command_buffer,
    };
    VK_ERR(vkQueueSubmit(queue, 1, &submit_info, NULL));
}

static inline void upload_data(void *data,
                               uint32_t size,
                               VkBufferUsageFlagBits usage,
                               VkBuffer *out_buffer,
                               VkDeviceMemory *out_memory) {

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    create_buffer(size,
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buffer,
                  &staging_buffer_memory);

    VkDeviceMemory buffer_memory;
    create_buffer(size,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  out_buffer,
                  &buffer_memory);

    *out_memory = buffer_memory;

    // Fill staging buffer
    void *mapped_data;
    VK_ERR(vkMapMemory(
        logical_device, staging_buffer_memory, 0, size, 0, &mapped_data));
    memcpy(mapped_data, data, size);
    vkUnmapMemory(logical_device, staging_buffer_memory);

    // Copy from staging buffer to actual buffer
    buffer_copy(*out_buffer, staging_buffer, size);

    vkQueueWaitIdle(queue);
    vkFreeMemory(logical_device, staging_buffer_memory, NULL);
    vkDestroyBuffer(logical_device, staging_buffer, NULL);
}

static inline void create_uniform_buffers(void) {

    memset(uniform_buffers, 0, sizeof uniform_buffers);
    memset(uniform_buffer_memories, 0, sizeof uniform_buffer_memories);
    memset(uniform_buffer_mappings, 0, sizeof uniform_buffer_mappings);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {

        VkDeviceSize size = sizeof(UniformData);
        create_buffer(size,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      uniform_buffers + i,
                      uniform_buffer_memories + i);

        VK_ERR(vkMapMemory(logical_device,
                           uniform_buffer_memories[i],
                           0,
                           size,
                           0,
                           uniform_buffer_mappings + i));
    }
}

static inline void create_descriptor_pool(void) {

    // Create descriptor pool
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = MAX_FRAMES_IN_FLIGHT,
    };
    VkDescriptorPoolCreateInfo pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        // .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    VK_ERR(vkCreateDescriptorPool(
        logical_device, &pool_create_info, NULL, &descriptor_pool));
}

static inline void create_descriptor_sets(void) {

    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT] = {0};

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        layouts[i] = descriptor_set_layout;

    VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts = layouts,
    };
    VK_ERR(vkAllocateDescriptorSets(
        logical_device, &descriptor_set_alloc_info, descriptor_sets));

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo buf_info = {
            .buffer = uniform_buffers[i],
            .offset = 0,
            .range = sizeof(UniformData),
        };
        VkWriteDescriptorSet descriptor_write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buf_info,
        };
        vkUpdateDescriptorSets(logical_device, 1, &descriptor_write, 0, NULL);
    }
}

static inline void create_command_buffers(void) {

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
    VK_ERR(
        vkAllocateCommandBuffers(logical_device, &alloc_info, command_buffers));
}

static inline void create_sync_objects(void) {

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
}

static inline void
transition_image_layout(uint32_t image_index,
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

    vkCmdPipelineBarrier2(command_buffers[frame_index], &dependency_info);
}

static inline void record_command_buffer(uint32_t image_index) {

    VkCommandBuffer cmd_buf = command_buffers[frame_index];

    // Begin command buffer
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_ERR(vkBeginCommandBuffer(cmd_buf, &begin_info));

    transition_image_layout(image_index,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            0,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    // Black
    VkClearValue clear_value = {0};
    VkRenderingAttachmentInfo attachment_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain_images.image_views[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear_value,
    };
    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = swap_extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment_info,
    };

    vkCmdBeginRendering(cmd_buf, &rendering_info);
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd_buf, 0, 1, &vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmd_buf, index_buffer, 0, VK_INDEX_TYPE_UINT32);

    VkViewport viewport = {0, 0, swap_extent.width, swap_extent.height, 0, 1};
    vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

    VkRect2D scissor = {.extent = swap_extent};
    vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd_buf,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout,
                            0,
                            1,
                            descriptor_sets + frame_index,
                            0,
                            NULL);
    vkCmdDrawIndexed(cmd_buf, COUNT(indices), 1, 0, 0, 0);

    vkCmdEndRendering(cmd_buf);
    transition_image_layout(image_index,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(cmd_buf);
}

static inline void recreate_swapchain(void) {

    VK_ERR(vkDeviceWaitIdle(logical_device));

    memset(swapchain_images.images, 0, sizeof swapchain_images.images);
    memset(
        swapchain_images.image_views, 0, sizeof swapchain_images.image_views);
    swapchain_images.count = SWAPCHAIN_MAX_IMAGES;

    create_swap_chain();
}

static inline void
vk_perspective(float fovy, float aspect, float nearZ, float farZ, mat4 dest) {
    glm_perspective(fovy, aspect, nearZ, farZ, dest);
    dest[1][1] *= -1;
}

static inline void update_uniform_buffer(uint32_t i) {
    struct timespec current_time = {0};
    ERR(clock_gettime(CLOCK_MONOTONIC, &current_time) < 0, "clock_gettime()");
    float current_time_seconds =
        current_time.tv_sec + current_time.tv_nsec / 1000000000.0;
    float rotation = 2 * M_PI * (fmod(current_time_seconds, 3) / 3.0);

    UniformData ubo_data = {
        .model = GLM_MAT4_IDENTITY_INIT,
        .view = GLM_MAT4_IDENTITY_INIT,
        .projection = GLM_MAT4_IDENTITY_INIT,
    };

    vec3 up = {0, 0, 1};
    vec3 eye = {1, 1, 1};
    vec3 target = {0, 0, 0};
    float aspect_ratio = (float)swap_extent.width / swap_extent.height;

    glm_rotate(ubo_data.model, rotation, up);
    glm_lookat(eye, target, up, ubo_data.view);
    vk_perspective(M_PI * 0.25, aspect_ratio, 0.1, 10, ubo_data.projection);

    memcpy(uniform_buffer_mappings[i], &ubo_data, sizeof ubo_data);
}

static inline void draw_frame(void) {

    VK_ERR(vkQueueWaitIdle(queue));

    uint32_t image_index;
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
        recreate_swapchain();
        return;

    default:
        VK_ERR_MSG(result, "vkAcquireNextImageKHR()");
        break;
    }

    record_command_buffer(image_index);

    update_uniform_buffer(frame_index);

    VK_ERR(vkResetFences(logical_device, 1, &draw_fences[frame_index]));

    VkPipelineStageFlags wait_destination_stage_mask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = present_done_semaphores + frame_index,
        .pWaitDstStageMask = &wait_destination_stage_mask,
        .commandBufferCount = 1,
        .pCommandBuffers = command_buffers + frame_index,
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

    result = vkQueuePresentKHR(queue, &present_info);

    switch (result) {
    case VK_SUCCESS:
        break;

    case VK_SUBOPTIMAL_KHR:
    case VK_ERROR_OUT_OF_DATE_KHR:
        recreate_swapchain();
        return;

    default:
        VK_ERR_MSG(result, "vkQueuePresentKHR()");
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    init_window(WINDOW_WIDTH, WINDOW_HEIGHT, "bricklayer2");

    create_instance();

    VK_ERR(glfwCreateWindowSurface(instance, window, NULL, &surface));

    pick_physical_device();
    create_logical_device();
    create_swap_chain();

    create_descriptor_set_layout();
    create_uniform_buffers();
    create_descriptor_pool();
    create_descriptor_sets();

    create_graphics_pipeline();

    create_command_buffers();

    VkDeviceMemory vertex_memory;
    VkDeviceMemory index_memory;
    upload_data(vertices,
                sizeof vertices,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                &vertex_buffer,
                &vertex_memory);
    upload_data(indices,
                sizeof indices,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                &index_buffer,
                &index_memory);

    create_sync_objects();

    printf("Done\n");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        draw_frame();
    }

    printf("Exiting\n");

    // Cleanup

    vkDeviceWaitIdle(logical_device);
    vkQueueWaitIdle(queue);

    vkDestroyPipeline(logical_device, pipeline, NULL);
    vkDestroyPipelineLayout(logical_device, pipeline_layout, NULL);
    vkDestroySwapchainKHR(logical_device, swapchain, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(logical_device, uniform_buffers[i], NULL);
        vkFreeMemory(logical_device, uniform_buffer_memories[i], NULL);

        vkDestroySemaphore(logical_device, present_done_semaphores[i], NULL);
        vkDestroySemaphore(logical_device, rendering_done_semaphores[i], NULL);
        vkDestroyFence(logical_device, draw_fences[i], NULL);
    }

    vkDestroyBuffer(logical_device, vertex_buffer, NULL);
    vkDestroyBuffer(logical_device, index_buffer, NULL);
    vkFreeMemory(logical_device, vertex_memory, NULL);
    vkFreeMemory(logical_device, index_memory, NULL);

    for (uint32_t i = 0; i < swapchain_images.count; i++) {
        vkDestroyImageView(
            logical_device, swapchain_images.image_views[i], NULL);
    }
    vkDestroyDescriptorSetLayout(logical_device, descriptor_set_layout, NULL);
    vkDestroyDescriptorPool(logical_device, descriptor_pool, NULL);

    vkDestroyCommandPool(logical_device, command_pool, NULL);

    vkDestroyDevice(logical_device, NULL);

    glfwDestroyWindow(window);
    glfwTerminate();

    exit(EXIT_SUCCESS);
}
