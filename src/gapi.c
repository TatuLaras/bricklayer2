#include "gapi.h"

#include "gapi_low_level.h"

#include "gapi_types.h"
#include "log.h"
#include "math_macros.h"
// #define VEC_INLINE_FUNCTIONS
#include "vec.h"
#include <vulkan/vulkan_core.h>

#define DEG_TO_RAD 0.01745329252

VEC(GapiObject, GapiObjectBuf)
VEC(GapiMesh, GapiMeshBuf)
VEC(GapiTexture, GapiTextureBuf)

VkResult gapi_vulkan_error = VK_SUCCESS;

static uint32_t frame_index = 0;
static uint32_t image_index = 0;
static uint32_t queue_index = 0;

static int has_window_resized = 0;
static GLFWwindow *window = NULL;

static GapiCamera scene_camera = {0};

static VkInstance instance = NULL;
static VkDevice device = NULL;
static VkPhysicalDevice physical_device = NULL;
static VkQueue queue = NULL;
static VkExtent2D swap_extent = {0};
static VkSwapchainKHR swapchain = NULL;
static VkSurfaceKHR surface = NULL;
static VkSurfaceFormatKHR surface_format = {0};
static VkCommandPool command_pool = NULL;
static struct {
    uint32_t count;
    VkImage images[SWAPCHAIN_MAX_IMAGES];
    VkImageView image_views[SWAPCHAIN_MAX_IMAGES];
} swapchain_images = {.count = SWAPCHAIN_MAX_IMAGES};

static VkSemaphore present_done_semaphores[GAPI_MAX_FRAMES_IN_FLIGHT] = {0};
static VkSemaphore rendering_done_semaphores[GAPI_MAX_FRAMES_IN_FLIGHT] = {0};
static VkFence draw_fences[GAPI_MAX_FRAMES_IN_FLIGHT] = {0};

static VkCommandBuffer drawing_command_buffers[GAPI_MAX_FRAMES_IN_FLIGHT] = {0};

static VkImage depth_image = NULL;
static VkDeviceMemory depth_image_memory;
static VkImageView depth_image_view;
static VkFormat depth_format = 0;

static VkDescriptorSetLayout descriptor_set_layout = NULL;

static VkPipeline *pipelines = NULL;
static VkPipelineLayout pipeline_layout = NULL;

static GapiObjectBuf objects = {0};
static GapiMeshBuf meshes = {0};
static GapiTextureBuf textures = {0};

// Destroy swapchain along with its image views.
static inline void destroy_swapchain(void) {

    for (uint32_t i = 0; i < swapchain_images.count; i++) {
        vkDestroyImageView(device, swapchain_images.image_views[i], NULL);
    }

    vkDestroySwapchainKHR(device, swapchain, NULL);
    swapchain = 0;

    memset(&swapchain_images, 0, sizeof swapchain_images);
    swapchain_images.count = SWAPCHAIN_MAX_IMAGES;
}

static inline GapiResult recreate_swapchain(void) {

    has_window_resized = 0;

    VK_ERR(vkDeviceWaitIdle(device));
    destroy_swapchain();
    PROPAGATE(gll_create_swapchain(device,
                                   physical_device,
                                   window,
                                   surface,
                                   &surface_format,
                                   &swap_extent,
                                   &swapchain));
    PROPAGATE(gll_create_swapchain_image_views(device,
                                               swapchain,
                                               surface_format,
                                               &swapchain_images.count,
                                               swapchain_images.images,
                                               swapchain_images.image_views));

    gll_destroy_depth_resources(
        device, depth_image, depth_image_memory, depth_image_view);
    PROPAGATE(gll_create_depth_resources(device,
                                         physical_device,
                                         swap_extent,
                                         &depth_format,
                                         &depth_image,
                                         &depth_image_memory,
                                         &depth_image_view));

    return GAPI_SUCCESS;
}

static inline GapiResult create_sync_objects(void) {

    PROPAGATE(gll_create_semaphores(
        device, GAPI_MAX_FRAMES_IN_FLIGHT, present_done_semaphores));
    PROPAGATE(gll_create_semaphores(
        device, GAPI_MAX_FRAMES_IN_FLIGHT, rendering_done_semaphores));
    PROPAGATE(
        gll_create_fences(device, GAPI_MAX_FRAMES_IN_FLIGHT, draw_fences));
    return GAPI_SUCCESS;
}

static void
window_resized_callback(GLFWwindow *_window, int _width, int _height) {
    (void)_window;
    (void)_width;
    (void)_height;

    has_window_resized = 1;
}

GapiResult gapi_init(GapiInitInfo *info) {

    if (GapiObjectBuf_init(&objects) < 0)
        return GAPI_INVALID_HANDLE;
    if (GapiMeshBuf_init(&meshes) < 0)
        return GAPI_INVALID_HANDLE;
    if (GapiTextureBuf_init(&textures) < 0)
        return GAPI_INVALID_HANDLE;

    PROPAGATE(gll_init_window(info->window.width,
                              info->window.height,
                              info->window.title,
                              info->window.flags,
                              window_resized_callback,
                              &window));
    PROPAGATE(gll_create_instance(&instance));
    VK_ERR(glfwCreateWindowSurface(instance, window, NULL, &surface));
    PROPAGATE(gll_create_device(
        instance, surface, &physical_device, &device, &queue_index, &queue));
    PROPAGATE(gll_create_swapchain(device,
                                   physical_device,
                                   window,
                                   surface,
                                   &surface_format,
                                   &swap_extent,
                                   &swapchain));
    PROPAGATE(gll_create_swapchain_image_views(device,
                                               swapchain,
                                               surface_format,
                                               &swapchain_images.count,
                                               swapchain_images.images,
                                               swapchain_images.image_views));
    PROPAGATE(gll_create_command_pool(device, queue_index, &command_pool));
    PROPAGATE(gll_create_command_buffers(device,
                                         command_pool,
                                         GAPI_MAX_FRAMES_IN_FLIGHT,
                                         drawing_command_buffers));
    PROPAGATE(gll_create_depth_resources(device,
                                         physical_device,
                                         swap_extent,
                                         &depth_format,
                                         &depth_image,
                                         &depth_image_memory,
                                         &depth_image_view));

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

    PROPAGATE(gll_create_descriptor_set_layout(device,
                                               COUNT(layout_bindings),
                                               layout_bindings,
                                               &descriptor_set_layout));

    ERR((pipelines = calloc(info->shader_count, sizeof(VkPipeline))) == NULL,
        "calloc()");

    for (uint32_t i = 0; i < info->shader_count; i++) {
        PROPAGATE(gll_create_graphics_pipeline(device,
                                               surface_format,
                                               info->shaders[i],
                                               descriptor_set_layout,
                                               depth_format,
                                               &pipeline_layout,
                                               pipelines));
    }

    PROPAGATE(create_sync_objects());

    return GAPI_SUCCESS;
}

GapiResult gapi_mesh_upload(MeshData *mesh, GapiMeshHandle *out_mesh_handle) {

    GapiMesh gpu_mesh = {.index_count = mesh->index_count};

    PROPAGATE(gll_upload_data(device,
                              physical_device,
                              command_pool,
                              queue,
                              mesh->vertices,
                              mesh->vertex_count * sizeof *mesh->vertices,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              &gpu_mesh.vertex_buffer,
                              &gpu_mesh.vertex_memory));
    PROPAGATE(gll_upload_data(device,
                              physical_device,
                              command_pool,
                              queue,
                              mesh->indices,
                              mesh->index_count * sizeof *mesh->indices,
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              &gpu_mesh.index_buffer,
                              &gpu_mesh.index_memory));

    GapiMeshHandle handle = meshes.count;
    SYS_ERR(GapiMeshBuf_append(&meshes, &gpu_mesh));

    *out_mesh_handle = handle;
    return GAPI_SUCCESS;
}

GapiResult gapi_texture_upload(uint32_t *pixels,
                               uint32_t width,
                               uint32_t height,
                               GapiTextureHandle *out_texture_handle) {

    GapiTexture texture = {0};

    PROPAGATE(gll_create_texture(device,
                                 command_pool,
                                 physical_device,
                                 queue,
                                 pixels,
                                 width,
                                 height,
                                 &texture));

    GapiTextureHandle handle = textures.count;
    SYS_ERR(GapiTextureBuf_append(&textures, &texture));

    *out_texture_handle = handle;
    return GAPI_SUCCESS;
}

GapiResult gapi_object_create(GapiMeshHandle mesh_handle,
                              GapiTextureHandle texture_handle,
                              GapiObjectHandle *out_object_handle) {

    GapiObject new_object = {
        .mesh_handle = mesh_handle,
        .texture_handle = texture_handle,
        .model_matrix = GLM_MAT4_IDENTITY_INIT,
    };
    GapiObjectHandle handle = objects.count;
    SYS_ERR(GapiObjectBuf_append(&objects, &new_object));

    GapiObject *object = objects.data + handle;

    for (uint32_t i = 0; i < GAPI_MAX_FRAMES_IN_FLIGHT; i++) {
        PROPAGATE(
            gll_create_uniform_buffer(device,
                                      physical_device,
                                      sizeof(GapiUBO),
                                      object->uniform_buffers + i,
                                      object->uniform_buffer_memories + i,
                                      object->uniform_buffer_mappings + i));
    }

    GapiTexture *texture = GapiTextureBuf_get(&textures, texture_handle);
    if (texture == NULL)
        return GAPI_INVALID_HANDLE;

    *out_object_handle = handle;
    return GAPI_SUCCESS;
}

VkResult gapi_get_vulkan_error(void) {
    return gapi_vulkan_error;
}

GapiResult gapi_render_begin(GapiCamera *camera) {

    if (camera != NULL)
        scene_camera = *camera;

    VK_ERR(vkQueueWaitIdle(queue));

    VkResult result =
        vkAcquireNextImageKHR(device,
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
        PROPAGATE(gapi_render_begin(NULL));
        return GAPI_SUCCESS;

    default:
        VK_ERR(result);
    }

    VkCommandBuffer cmd_buf = drawing_command_buffers[frame_index];

    // Begin command buffer
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_ERR(vkBeginCommandBuffer(cmd_buf, &begin_info));

    gll_transition_image_layout(
        device,
        command_pool,
        queue,
        depth_image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    gll_transition_swapchain_image_layout(
        cmd_buf,
        swapchain_images.images[image_index],
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

    return GAPI_SUCCESS;
}

GapiResult gapi_render_end(void) {

    VkCommandBuffer cmd_buf = drawing_command_buffers[frame_index];

    vkCmdEndRendering(cmd_buf);
    gll_transition_swapchain_image_layout(
        cmd_buf,
        swapchain_images.images[image_index],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(cmd_buf);

    VK_ERR(vkResetFences(device, 1, &draw_fences[frame_index]));

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
        device, 1, draw_fences + frame_index, VK_TRUE, UINT64_MAX));

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = rendering_done_semaphores + frame_index,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };

    frame_index = (frame_index + 1) % GAPI_MAX_FRAMES_IN_FLIGHT;

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

    return GAPI_SUCCESS;
}

static inline void update_uniform_buffer(GapiObject *object, mat4 *matrix) {

    GapiUBO ubo_data = {
        .view = GLM_MAT4_IDENTITY_INIT,
        .projection = GLM_MAT4_IDENTITY_INIT,
    };
    memcpy(ubo_data.model, matrix, sizeof(mat4));

    float aspect_ratio = (float)swap_extent.width / swap_extent.height;

    glm_lookat(
        scene_camera.pos, scene_camera.target, scene_camera.up, ubo_data.view);
    glm_perspective(DEG_TO_RAD * scene_camera.fov_degrees,
                    aspect_ratio,
                    0.1,
                    100,
                    ubo_data.projection);
    ubo_data.projection[1][1] *= -1;

    memcpy(object->uniform_buffer_mappings[frame_index],
           &ubo_data,
           sizeof ubo_data);
}

void gapi_object_draw(GapiObjectHandle object_handle, mat4 *matrix) {

    VkCommandBuffer cmd_buf = drawing_command_buffers[frame_index];
    GapiObject *object = GapiObjectBuf_get(&objects, object_handle);
    assert(object != NULL);
    GapiMesh *mesh = meshes.data + object->mesh_handle;

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd_buf, 0, 1, &mesh->vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmd_buf, mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);

    update_uniform_buffer(object, matrix);

    // TODO: Work without texture too
    GapiTexture *texture =
        GapiTextureBuf_get(&textures, object->texture_handle);
    if (texture == NULL) {
        return;
    }
    gll_push_descriptor_set(cmd_buf,
                            pipeline_layout,
                            texture,
                            object->uniform_buffers[frame_index]);

    vkCmdDrawIndexed(cmd_buf, mesh->index_count, 1, 0, 0, 0);
}

int gapi_window_should_close(void) {
    glfwPollEvents();
    return glfwWindowShouldClose(window);
}

const char *gapi_strerror(GapiResult result) {
    switch (result) {
    case GAPI_SUCCESS:
        return "Success";
    case GAPI_ERROR_GENERIC:
        return "Unknown error";
    case GAPI_INVALID_HANDLE:
        return "Invalid handle";
    case GAPI_SYSTEM_ERROR:
        return "A system error occurred, check errno";
    case GAPI_VULKAN_ERROR:
        return "A vulkan error occurred, check gapi_get_vulkan_error()";
    case GAPI_GLFW_ERROR:
        return "A glfw error occurred";
    case GAPI_NO_DEVICE_FOUND:
        return "No suitable physical device found";
    case GAPI_VULKAN_FEATURE_UNSUPPORTED:
        return "A required Vulkan feature is not supported";
    }

    // Theres a compiler warning I want to get rid of:
    return "";
}
