#ifndef _GAPI_LOW_LEVEL
#define _GAPI_LOW_LEVEL

#include "gapi.h"
#include "log.h"

// Creates a Vulkan instance and writes it out to `out_instance`.
GapiResult gll_instance_create(VkInstance *out_instance);
// Creates a logical device and writes it to `out_device`, along with the device
// queue into `out_queue`. `queue_index` is the device queue index obtained from
// gll_pick_physical_device() or index of another queue suitable for graphics
// and present.
GapiResult gll_device_create(VkInstance instance,
                             VkSurfaceKHR surface,
                             VkPhysicalDevice *out_physical_device,
                             VkDevice *out_device,
                             uint32_t *out_queue_index,
                             VkQueue *out_queue);
// Initializes a GLFW window.
GapiResult gll_window_init(uint32_t width,
                           uint32_t height,
                           const char *title,
                           GapiWindowFlags flags,
                           GLFWframebuffersizefun window_resize_callback,
                           GLFWwindow **out_window);

// Create a swapchain and return its extent, format and handle into
// `out_swap_extent`, `out_surface_format` and `out_swapchain`, respectively.
GapiResult gll_swapchain_create(VkDevice device,
                                VkPhysicalDevice physical_device,
                                GLFWwindow *window,
                                VkSurfaceKHR surface,
                                VkSurfaceFormatKHR *out_surface_format,
                                VkExtent2D *out_swap_extent,
                                VkSwapchainKHR *out_swapchain);
// Get swapchain images and create image views for them. The count of images
// will be returned into `swapchain_image_count` and that number of items
// will be written into both `out_swapchain_images` and
// `out_swapchain_image_views`.
GapiResult
gll_swapchain_image_views_create(VkDevice device,
                                 VkSwapchainKHR swapchain,
                                 VkSurfaceFormatKHR surface_format,
                                 uint32_t *swapchain_image_count,
                                 VkImage *out_swapchain_images,
                                 VkImageView *out_swapchain_image_views);

GapiResult gll_command_pool_create(VkDevice device,
                                   uint32_t queue_index,
                                   VkCommandPool *out_command_pool);
// Create `count` command buffers and return them into
// `out_command_buffers`.
GapiResult gll_command_buffers_create(VkDevice device,
                                      VkCommandPool command_pool,
                                      uint32_t count,
                                      VkCommandBuffer *out_command_buffers);

// Create an image resource.
GapiResult gll_image_create(VkDevice device,
                            VkPhysicalDevice physical_device,
                            uint32_t width,
                            uint32_t height,
                            VkFormat format,
                            VkImageTiling tiling,
                            VkImageUsageFlags usage,
                            VkMemoryPropertyFlags properties,
                            VkImage *out_image,
                            VkDeviceMemory *out_memory);
// Create image resource suitable for use as a depth buffer.
GapiResult gll_depth_resources_create(VkDevice device,
                                      VkPhysicalDevice physical_device,
                                      VkExtent2D swap_extent,
                                      VkFormat *out_depth_format,
                                      VkImage *out_depth_image,
                                      VkDeviceMemory *out_depth_image_memory,
                                      VkImageView *out_depth_image_view);
void gll_image_resources_destroy(VkDevice device,
                                 VkImage image,
                                 VkDeviceMemory memory,
                                 VkImageView image_view);

// Create a descriptor set layout. `bindings` is an array of
// VkDescriptorSetLayoutBinding structs of count `bindings_count`.
//  TODO: Depends on the shader used, dynamically create?
GapiResult gll_descritor_set_layout_create(
    VkDevice device,
    uint32_t bindings_count,
    VkDescriptorSetLayoutBinding *bindings,
    VkDescriptorSetLayout *out_descriptor_set_layout);

GapiResult gll_pipeline_create(VkDevice device,
                               VkSurfaceFormatKHR surface_format,
                               VkShaderModule shader_module,
                               VkDescriptorSetLayout descriptor_set_layout,
                               VkFormat depth_format,
                               GapiPipelineCreateInfo *create_info,
                               VkPipelineLayout *out_pipeline_layout,
                               VkPipeline *out_pipeline);

GapiResult gll_buffer_create(VkDevice device,
                             VkPhysicalDevice physical_device,
                             VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags memory_property_flags,
                             VkBuffer *out_buffer,
                             VkDeviceMemory *out_memory);
void gll_buffer_destroy(VkDevice device,
                        VkBuffer buffer,
                        VkDeviceMemory memory);
// Upload `data` of `size` into a staging buffer and then create the final
// buffer `out_buffer` and copy the data there.
GapiResult gll_upload_data(VkDevice device,
                           VkPhysicalDevice physical_device,
                           VkCommandBuffer command_buffer,
                           VkQueue queue,
                           void *data,
                           uint32_t size,
                           VkBufferUsageFlagBits usage,
                           VkBuffer *out_buffer,
                           VkDeviceMemory *out_memory);
GapiResult gll_uniform_buffer_create(VkDevice device,
                                     VkPhysicalDevice physical_device,
                                     VkDeviceSize size,
                                     VkBuffer *out_buffer,
                                     VkDeviceMemory *out_memory,
                                     void **out_mapping);
GapiResult gll_texture_create(VkDevice device,
                              VkCommandBuffer command_buffer,
                              VkPhysicalDevice physical_device,
                              VkQueue queue,
                              uint32_t *pixels,
                              uint32_t width,
                              uint32_t height,
                              GapiTexture *out_texture);

void gll_texture_destroy(VkDevice device, GapiTexture *texture);
void gll_mesh_destroy(VkDevice device, GapiMesh *mesh);

void gll_transition_swapchain_image_layout(
    VkCommandBuffer command_buffer,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags2 src_access_mask,
    VkAccessFlags2 dst_access_mask,
    VkPipelineStageFlags2 src_stage_mask,
    VkPipelineStageFlags2 dst_stage_mask);
void gll_transition_image_layout(VkCommandBuffer command_buffer,
                                 VkQueue queue,
                                 VkImage image,
                                 VkImageLayout old_layout,
                                 VkImageLayout new_layout,
                                 VkAccessFlags src_access_mask,
                                 VkAccessFlags dst_access_mask,
                                 VkPipelineStageFlags src_stage,
                                 VkPipelineStageFlags dst_stage,
                                 VkImageAspectFlags image_aspect_flags);

void gll_push_descriptor_set(VkCommandBuffer command_buffer,
                             VkPipelineLayout pipeline_layout,
                             GapiTexture *texture,
                             VkBuffer uniform_buffer);

GapiResult gll_semaphores_create(VkDevice device,
                                 uint32_t count,
                                 VkSemaphore *out_semaphores);
GapiResult
gll_fences_create(VkDevice device, uint32_t count, VkFence *out_fences);

#endif
