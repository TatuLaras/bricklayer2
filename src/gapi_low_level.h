#ifndef _GAPI_LOW_LEVEL
#define _GAPI_LOW_LEVEL

#include "gapi.h"
#include "log.h"

// Creates a Vulkan instance and writes it out to `out_instance`.
GapiResult gll_create_instance(VkInstance *out_instance);
// Creates a logical device and writes it to `out_device`, along with the device
// queue into `out_queue`. `queue_index` is the device queue index obtained from
// gll_pick_physical_device() or index of another queue suitable for graphics
// and present.
GapiResult gll_create_device(VkInstance instance,
                             VkSurfaceKHR surface,
                             VkPhysicalDevice *out_physical_device,
                             VkDevice *out_device,
                             uint32_t *out_queue_index,
                             VkQueue *out_queue);
// Initializes a GLFW window.
GapiResult gll_init_window(uint32_t width,
                           uint32_t height,
                           const char *title,
                           GapiWindowFlags flags,
                           GLFWframebuffersizefun window_resize_callback,
                           GLFWwindow **out_window);

// Create a swapchain and return its extent, format and handle into
// `out_swap_extent`, `out_surface_format` and `out_swapchain`, respectively.
GapiResult gll_create_swapchain(VkDevice device,
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
gll_create_swapchain_image_views(VkDevice device,
                                 VkSwapchainKHR swapchain,
                                 VkSurfaceFormatKHR surface_format,
                                 uint32_t *swapchain_image_count,
                                 VkImage *out_swapchain_images,
                                 VkImageView *out_swapchain_image_views);

GapiResult gll_create_command_pool(VkDevice device,
                                   uint32_t queue_index,
                                   VkCommandPool *out_command_pool);
// Create `count` command buffers and return them into
// `out_command_buffers`.
GapiResult gll_create_command_buffers(VkDevice device,
                                      VkCommandPool command_pool,
                                      uint32_t count,
                                      VkCommandBuffer *out_command_buffers);

// Create an image resource.
GapiResult gll_create_image(VkDevice device,
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
GapiResult gll_create_depth_resources(VkDevice device,
                                      VkPhysicalDevice physical_device,
                                      VkExtent2D swap_extent,
                                      VkFormat *out_depth_format,
                                      VkImage *out_depth_image,
                                      VkDeviceMemory *out_depth_image_memory,
                                      VkImageView *out_depth_image_view);
void gll_destroy_depth_resources(VkDevice device,
                                 VkImage image,
                                 VkDeviceMemory memory,
                                 VkImageView image_view);

// Create a descriptor set layout. `bindings` is an array of
// VkDescriptorSetLayoutBinding structs of count `bindings_count`.
//  TODO: Depends on the shader used, dynamically create?
GapiResult gll_create_descriptor_set_layout(
    VkDevice device,
    uint32_t bindings_count,
    VkDescriptorSetLayoutBinding *bindings,
    VkDescriptorSetLayout *out_descriptor_set_layout);

GapiResult
gll_create_graphics_pipeline(VkDevice device,
                             VkSurfaceFormatKHR surface_format,
                             GapiShader shader,
                             VkDescriptorSetLayout descriptor_set_layout,
                             VkFormat depth_format,
                             VkPipelineLayout *out_pipeline_layout,
                             VkPipeline *out_pipeline);

GapiResult gll_create_buffer(VkDevice device,
                             VkPhysicalDevice physical_device,
                             VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags memory_property_flags,
                             VkBuffer *out_buffer,
                             VkDeviceMemory *out_memory);
void gll_destroy_buffer(VkDevice device,
                        VkBuffer buffer,
                        VkDeviceMemory memory);
// Upload `data` of `size` into a staging buffer and then create the final
// buffer `out_buffer` and copy the data there.
GapiResult gll_upload_data(VkDevice device,
                           VkPhysicalDevice physical_device,
                           VkCommandPool command_pool,
                           VkQueue queue,
                           void *data,
                           uint32_t size,
                           VkBufferUsageFlagBits usage,
                           VkBuffer *out_buffer,
                           VkDeviceMemory *out_memory);
GapiResult gll_create_uniform_buffer(VkDevice device,
                                     VkPhysicalDevice physical_device,
                                     VkDeviceSize size,
                                     VkBuffer *out_buffer,
                                     VkDeviceMemory *out_memory,
                                     void **out_mapping);
GapiResult gll_create_texture(VkDevice device,
                              VkCommandPool command_pool,
                              VkPhysicalDevice physical_device,
                              VkQueue queue,
                              uint32_t *pixels,
                              uint32_t width,
                              uint32_t height,
                              GapiTexture *out_texture);

void gll_transition_swapchain_image_layout(
    VkCommandBuffer command_buffer,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags2 src_access_mask,
    VkAccessFlags2 dst_access_mask,
    VkPipelineStageFlags2 src_stage_mask,
    VkPipelineStageFlags2 dst_stage_mask);
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
                                 VkImageAspectFlags image_aspect_flags);

void gll_push_descriptor_set(VkCommandBuffer command_buffer,
                             VkPipelineLayout pipeline_layout,
                             GapiTexture *texture,
                             VkBuffer uniform_buffer);

GapiResult gll_create_semaphores(VkDevice device,
                                 uint32_t count,
                                 VkSemaphore *out_semaphores);
GapiResult
gll_create_fences(VkDevice device, uint32_t count, VkFence *out_fences);

#endif
