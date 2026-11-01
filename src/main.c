#define _CRT_SECURE_NO_WARNINGS
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_UI_VERTICES 65536
#define MAX_FRAMES_IN_FLIGHT 2

/* -------------------------------------------------------------------------- */
/* Immediate Mode UI System                                                  */
/* -------------------------------------------------------------------------- */

typedef uint32_t UiId;

typedef struct {
    float x, y;
    float r, g, b, a;
} UiVertex;

typedef struct {
    float screen_width;
    float screen_height;
} UiPushConstants;

typedef struct {
    float mouse_x;
    float mouse_y;
    bool mouse_down;
    bool mouse_pressed;
    bool mouse_released;

    UiId hot_id;
    UiId active_id;

    UiVertex vertices[MAX_UI_VERTICES];
    uint32_t vertex_count;
} UiContext;

static inline UiId ui_hash_id(const char *label) {
    uint32_t hash = 2166136261u;
    while (*label) {
        hash ^= (uint8_t)(*label++);
        hash *= 16777619u;
    }
    return hash;
}

static void ui_init(UiContext *ui_ctx) {
    memset(ui_ctx, 0, sizeof(UiContext));
}

static void ui_begin_frame(UiContext *ui_ctx, float mouse_x, float mouse_y,
                           bool mouse_down, bool mouse_pressed, bool mouse_released) {
    ui_ctx->mouse_x = mouse_x;
    ui_ctx->mouse_y = mouse_y;
    ui_ctx->mouse_down = mouse_down;
    ui_ctx->mouse_pressed = mouse_pressed;
    ui_ctx->mouse_released = mouse_released;
    ui_ctx->hot_id = 0;
    ui_ctx->vertex_count = 0;
}

static void ui_draw_rect(UiContext *ui_ctx, float x, float y, float width, float height,
                         float r, float g, float b, float a) {
    if (ui_ctx->vertex_count + 6 > MAX_UI_VERTICES) {
        return;
    }

    float x0 = x;
    float y0 = y;
    float x1 = x + width;
    float y1 = y + height;

    UiVertex v0 = { x0, y0, r, g, b, a };
    UiVertex v1 = { x1, y0, r, g, b, a };
    UiVertex v2 = { x1, y1, r, g, b, a };
    UiVertex v3 = { x0, y1, r, g, b, a };

    /* Two clockwise triangles forming a quad */
    ui_ctx->vertices[ui_ctx->vertex_count++] = v0;
    ui_ctx->vertices[ui_ctx->vertex_count++] = v1;
    ui_ctx->vertices[ui_ctx->vertex_count++] = v2;

    ui_ctx->vertices[ui_ctx->vertex_count++] = v0;
    ui_ctx->vertices[ui_ctx->vertex_count++] = v2;
    ui_ctx->vertices[ui_ctx->vertex_count++] = v3;
}

static void ui_panel(UiContext *ui_ctx, float x, float y, float width, float height,
                     float r, float g, float b, float a) {
    ui_draw_rect(ui_ctx, x, y, width, height, r, g, b, a);
}

static bool ui_button(UiContext *ui_ctx, const char *label, float x, float y, float width, float height) {
    UiId id = ui_hash_id(label);
    bool clicked = false;

    bool is_hovered = (ui_ctx->mouse_x >= x && ui_ctx->mouse_x <= x + width &&
                       ui_ctx->mouse_y >= y && ui_ctx->mouse_y <= y + height);

    if (is_hovered) {
        ui_ctx->hot_id = id;
    }

    if (ui_ctx->active_id == id) {
        if (ui_ctx->mouse_released) {
            if (is_hovered) {
                clicked = true;
            }
            ui_ctx->active_id = 0;
        }
    } else if (ui_ctx->hot_id == id) {
        if (ui_ctx->mouse_pressed) {
            ui_ctx->active_id = id;
        }
    }

    float r = 0.25f, g = 0.25f, b = 0.28f, a = 1.0f;
    if (ui_ctx->active_id == id) {
        r = 0.15f; g = 0.45f; b = 0.85f;
    } else if (ui_ctx->hot_id == id) {
        r = 0.35f; g = 0.35f; b = 0.40f;
    }

    ui_draw_rect(ui_ctx, x, y, width, height, r, g, b, a);
    return clicked;
}

static bool ui_slider(UiContext *ui_ctx, const char *label, float x, float y,
                      float width, float height, float *value, float min_val, float max_val) {
    UiId id = ui_hash_id(label);
    bool changed = false;

    bool is_hovered = (ui_ctx->mouse_x >= x && ui_ctx->mouse_x <= x + width &&
                       ui_ctx->mouse_y >= y && ui_ctx->mouse_y <= y + height);

    if (is_hovered) {
        ui_ctx->hot_id = id;
    }

    if (ui_ctx->active_id == id) {
        if (ui_ctx->mouse_down) {
            float normalized = (ui_ctx->mouse_x - x) / width;
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;
            float new_val = min_val + normalized * (max_val - min_val);
            if (*value != new_val) {
                *value = new_val;
                changed = true;
            }
        } else {
            ui_ctx->active_id = 0;
        }
    } else if (ui_ctx->hot_id == id && ui_ctx->mouse_pressed) {
        ui_ctx->active_id = id;
    }

    ui_draw_rect(ui_ctx, x, y, width, height, 0.15f, 0.15f, 0.18f, 1.0f);

    float fill_pct = (*value - min_val) / (max_val - min_val);
    if (fill_pct < 0.0f) fill_pct = 0.0f;
    if (fill_pct > 1.0f) fill_pct = 1.0f;
    ui_draw_rect(ui_ctx, x, y, width * fill_pct, height, 0.20f, 0.55f, 0.90f, 1.0f);

    return changed;
}

/* -------------------------------------------------------------------------- */
/* Vulkan Helpers                                                             */
/* -------------------------------------------------------------------------- */

static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

static VkShaderModule load_shader_module(VkDevice device, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        SDL_Log("Failed to open shader file: %s", path);
        return VK_NULL_HANDLE;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *code = malloc(length);
    if (!code) {
        fclose(file);
        return VK_NULL_HANDLE;
    }

    size_t read_bytes = fread(code, 1, length, file);
    fclose(file);

    if (read_bytes != (size_t)length) {
        free(code);
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = length,
        .pCode = (const uint32_t *)code,
    };

    VkShaderModule shader_module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &create_info, NULL, &shader_module) != VK_SUCCESS) {
        SDL_Log("Failed to create shader module for %s", path);
    }

    free(code);
    return shader_module;
}

/* -------------------------------------------------------------------------- */
/* Swapchain Context & Management                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    VkSwapchainKHR handle;
    VkFormat image_format;
    VkExtent2D extent;
    uint32_t image_count;
    VkImage *images;
    VkImageView *image_views;
    VkFramebuffer *framebuffers;
} VulkanSwapchain;

static void cleanup_swapchain(VkDevice device, VulkanSwapchain *swapchain) {
    if (swapchain->framebuffers) {
        for (uint32_t i = 0; i < swapchain->image_count; ++i) {
            vkDestroyFramebuffer(device, swapchain->framebuffers[i], NULL);
        }
        free(swapchain->framebuffers);
        swapchain->framebuffers = NULL;
    }

    if (swapchain->image_views) {
        for (uint32_t i = 0; i < swapchain->image_count; ++i) {
            vkDestroyImageView(device, swapchain->image_views[i], NULL);
        }
        free(swapchain->image_views);
        swapchain->image_views = NULL;
    }

    if (swapchain->images) {
        free(swapchain->images);
        swapchain->images = NULL;
    }

    if (swapchain->handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain->handle, NULL);
        swapchain->handle = VK_NULL_HANDLE;
    }
}

static bool create_swapchain(SDL_Window *window, VkPhysicalDevice physical_device,
                             VkDevice device, VkSurfaceKHR surface,
                             VkRenderPass render_pass, VulkanSwapchain *swapchain) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL);
    VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats);

    VkSurfaceFormatKHR surface_format = formats[0];
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = formats[i];
            break;
        }
    }
    free(formats);

    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent = capabilities.currentExtent;
    } else {
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        extent.width = (uint32_t)w;
        extent.height = (uint32_t)h;
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain->handle) != VK_SUCCESS) {
        return false;
    }

    swapchain->image_format = surface_format.format;
    swapchain->extent = extent;

    vkGetSwapchainImagesKHR(device, swapchain->handle, &swapchain->image_count, NULL);
    swapchain->images = malloc(sizeof(VkImage) * swapchain->image_count);
    vkGetSwapchainImagesKHR(device, swapchain->handle, &swapchain->image_count, swapchain->images);

    swapchain->image_views = malloc(sizeof(VkImageView) * swapchain->image_count);
    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain->image_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vkCreateImageView(device, &view_info, NULL, &swapchain->image_views[i]);
    }

    swapchain->framebuffers = malloc(sizeof(VkFramebuffer) * swapchain->image_count);
    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        VkFramebufferCreateInfo fb_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = render_pass,
            .attachmentCount = 1,
            .pAttachments = &swapchain->image_views[i],
            .width = extent.width,
            .height = extent.height,
            .layers = 1,
        };
        vkCreateFramebuffer(device, &fb_info, NULL, &swapchain->framebuffers[i]);
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Main Program                                                               */
/* -------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    const int initial_width = 1280;
    const int initial_height = 720;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Immediate Mode UI - SDL3 & Vulkan",
        initial_width,
        initial_height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    /* 1. Vulkan Instance */
    Uint32 sdl_extension_count = 0;
    char const * const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
    if (!sdl_extensions) {
        SDL_Log("Failed to get Vulkan instance extensions: %s", SDL_GetError());
        return -1;
    }

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "SDL3 Vulkan IMGUI",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Custom",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_2,
    };

    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = sdl_extension_count,
        .ppEnabledExtensionNames = sdl_extensions,
    };

    VkInstance vk_instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instance_info, NULL, &vk_instance) != VK_SUCCESS) {
        SDL_Log("Failed to create Vulkan instance");
        return -1;
    }

    /* 2. Surface Creation */
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, vk_instance, NULL, &vk_surface)) {
        SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
        return -1;
    }

    /* 3. Pick Physical Device & Find Queue Family */
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(vk_instance, &physical_device_count, NULL);
    if (physical_device_count == 0) {
        SDL_Log("No Vulkan physical devices found");
        return -1;
    }
    VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * physical_device_count);
    vkEnumeratePhysicalDevices(vk_instance, &physical_device_count, physical_devices);
    VkPhysicalDevice physical_device = physical_devices[0];

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);
    VkQueueFamilyProperties *queue_families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);

    uint32_t graphics_queue_family = 0;
    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            SDL_Vulkan_GetPresentationSupport(vk_instance, physical_device, i)) {
            graphics_queue_family = i;
            break;
        }
    }
    free(queue_families);
    free(physical_devices);

    /* 4. Create Logical Device */
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
    };

    VkDevice vk_device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical_device, &device_info, NULL, &vk_device) != VK_SUCCESS) {
        SDL_Log("Failed to create logical device");
        return -1;
    }

    VkQueue graphics_queue;
    vkGetDeviceQueue(vk_device, graphics_queue_family, 0, &graphics_queue);

    /* 5. Render Pass */
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, vk_surface, &format_count, NULL);
    VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, vk_surface, &format_count, formats);
    VkFormat surface_format = formats[0].format;
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = formats[i].format;
            break;
        }
    }
    free(formats);

    VkAttachmentDescription color_attachment = {
        .format = surface_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_attachment_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VkRenderPass render_pass;
    vkCreateRenderPass(vk_device, &render_pass_info, NULL, &render_pass);

    /* 6. Swapchain Setup */
    VulkanSwapchain swapchain = {0};
    if (!create_swapchain(window, physical_device, vk_device, vk_surface, render_pass, &swapchain)) {
        SDL_Log("Failed to create swapchain");
        return -1;
    }

    /* 7. Pipeline Layout & Shaders */
    VkShaderModule vert_module = load_shader_module(vk_device, "ui.vert.spv");
    VkShaderModule frag_module = load_shader_module(vk_device, "ui.frag.spv");
    if (!vert_module || !frag_module) {
        SDL_Log("Failed to load shaders! Make sure ui.vert.spv and ui.frag.spv exist.");
        return -1;
    }

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName = "main",
        },
    };

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(UiPushConstants), /* Matches vec2 screen_size */
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VkPipelineLayout pipeline_layout;
    vkCreatePipelineLayout(vk_device, &pipeline_layout_info, NULL, &pipeline_layout);

    /* Vertex input format */
    VkVertexInputBindingDescription binding_description = {
        .binding = 0,
        .stride = sizeof(UiVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attribute_descriptions[2] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(UiVertex, x),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(UiVertex, r),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_description,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attribute_descriptions,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    /* Alpha blending for translucent UI panels */
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state_info,
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
    };

    VkPipeline ui_pipeline;
    vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &ui_pipeline);

    vkDestroyShaderModule(vk_device, frag_module, NULL);
    vkDestroyShaderModule(vk_device, vert_module, NULL);

    /* 8. Command Pool & Multi-Buffering Resources */
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphics_queue_family,
    };

    VkCommandPool command_pool;
    vkCreateCommandPool(vk_device, &pool_info, NULL, &command_pool);

    VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    vkAllocateCommandBuffers(vk_device, &alloc_info, command_buffers);

    /* Vertex buffers (one per in-flight frame to avoid stalls) */
    VkBuffer vertex_buffers[MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory vertex_buffer_memories[MAX_FRAMES_IN_FLIGHT];
    void *mapped_vertex_ptrs[MAX_FRAMES_IN_FLIGHT];
    VkDeviceSize buffer_size = sizeof(UiVertex) * MAX_UI_VERTICES;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buffer_size,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(vk_device, &buffer_info, NULL, &vertex_buffers[i]);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(vk_device, vertex_buffers[i], &mem_reqs);

        VkMemoryAllocateInfo mem_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = find_memory_type(
                physical_device,
                mem_reqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ),
        };
        vkAllocateMemory(vk_device, &mem_alloc_info, NULL, &vertex_buffer_memories[i]);
        vkBindBufferMemory(vk_device, vertex_buffers[i], vertex_buffer_memories[i], 0);
        vkMapMemory(vk_device, vertex_buffer_memories[i], 0, buffer_size, 0, &mapped_vertex_ptrs[i]);
    }

    /* Synchronizations */
    VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished_semaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];

    VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkCreateSemaphore(vk_device, &semaphore_info, NULL, &image_available_semaphores[i]);
        vkCreateSemaphore(vk_device, &semaphore_info, NULL, &render_finished_semaphores[i]);
        vkCreateFence(vk_device, &fence_info, NULL, &in_flight_fences[i]);
    }

    /* ---------------------------------------------------------------------- */
    /* Application / UI State Loop                                            */
    /* ---------------------------------------------------------------------- */
    UiContext ui_ctx;
    ui_init(&ui_ctx);

    bool running = true;
    int click_counter = 0;
    float volume_level = 0.5f;
    uint32_t current_frame = 0;
    bool framebuffer_resized = false;

    while (running) {
        bool mouse_pressed = false;
        bool mouse_released = false;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse_pressed = true;
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse_released = true;
                }
            } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                framebuffer_resized = true;
            }
        }

        float mouse_x, mouse_y;
        SDL_MouseButtonFlags mouse_state = SDL_GetMouseState(&mouse_x, &mouse_y);
        bool mouse_down = (mouse_state & SDL_BUTTON_LMASK) != 0;

        /* --- UI Definition Phase --- */
        ui_begin_frame(&ui_ctx, mouse_x, mouse_y, mouse_down, mouse_pressed, mouse_released);

        ui_panel(&ui_ctx, 50.0f, 50.0f, 320.0f, 220.0f, 0.10f, 0.10f, 0.12f, 0.90f);

        if (ui_button(&ui_ctx, "btn_increment", 70.0f, 80.0f, 150.0f, 40.0f)) {
            click_counter++;
            SDL_Log("Button Clicked! Total: %d", click_counter);
        }

        ui_slider(&ui_ctx, "sld_volume", 70.0f, 150.0f, 280.0f, 20.0f, &volume_level, 0.0f, 1.0f);

        /* --- Vulkan Rendering Phase --- */
        vkWaitForFences(vk_device, 1, &in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);

        uint32_t image_index;
        VkResult acquire_result = vkAcquireNextImageKHR(
            vk_device, swapchain.handle, UINT64_MAX,
            image_available_semaphores[current_frame], VK_NULL_HANDLE, &image_index
        );

        if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR || framebuffer_resized) {
            framebuffer_resized = false;
            vkDeviceWaitIdle(vk_device);
            cleanup_swapchain(vk_device, &swapchain);
            create_swapchain(window, physical_device, vk_device, vk_surface, render_pass, &swapchain);
            continue;
        }

        vkResetFences(vk_device, 1, &in_flight_fences[current_frame]);

        /* Upload UI geometry to current frame's host-visible vertex buffer */
        if (ui_ctx.vertex_count > 0) {
            memcpy(mapped_vertex_ptrs[current_frame], ui_ctx.vertices, ui_ctx.vertex_count * sizeof(UiVertex));
        }

        /* Record draw commands */
        VkCommandBuffer cmd = command_buffers[current_frame];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo cmd_begin = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        vkBeginCommandBuffer(cmd, &cmd_begin);

        VkClearValue clear_color = {{{ 0.05f, 0.05f, 0.07f, 1.0f }}};
        VkRenderPassBeginInfo rp_begin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = render_pass,
            .framebuffer = swapchain.framebuffers[image_index],
            .renderArea.offset = {0, 0},
            .renderArea.extent = swapchain.extent,
            .clearValueCount = 1,
            .pClearValues = &clear_color,
        };

        vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = (float)swapchain.extent.width,
            .height = (float)swapchain.extent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {
            .offset = {0, 0},
            .extent = swapchain.extent,
        };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        if (ui_ctx.vertex_count > 0) {
            UiPushConstants push_constants = {
                .screen_width = (float)swapchain.extent.width,
                .screen_height = (float)swapchain.extent.height,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline);
            vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UiPushConstants), &push_constants);

            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffers[current_frame], &offset);
            vkCmdDraw(cmd, ui_ctx.vertex_count, 1, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        /* Submit to Graphics Queue */
        VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &image_available_semaphores[current_frame],
            .pWaitDstStageMask = wait_stages,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &render_finished_semaphores[current_frame],
        };
        vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fences[current_frame]);

        /* Present to Swapchain */
        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_finished_semaphores[current_frame],
            .swapchainCount = 1,
            .pSwapchains = &swapchain.handle,
            .pImageIndices = &image_index,
        };

        VkResult present_result = vkQueuePresentKHR(graphics_queue, &present_info);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
            framebuffer_resized = true;
        }

        current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    /* Cleanup */
    vkDeviceWaitIdle(vk_device);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(vk_device, image_available_semaphores[i], NULL);
        vkDestroySemaphore(vk_device, render_finished_semaphores[i], NULL);
        vkDestroyFence(vk_device, in_flight_fences[i], NULL);
        vkUnmapMemory(vk_device, vertex_buffer_memories[i]);
        vkDestroyBuffer(vk_device, vertex_buffers[i], NULL);
        vkFreeMemory(vk_device, vertex_buffer_memories[i], NULL);
    }

    cleanup_swapchain(vk_device, &swapchain);
    vkDestroyCommandPool(vk_device, command_pool, NULL);
    vkDestroyPipeline(vk_device, ui_pipeline, NULL);
    vkDestroyPipelineLayout(vk_device, pipeline_layout, NULL);
    vkDestroyRenderPass(vk_device, render_pass, NULL);
    vkDestroyDevice(vk_device, NULL);
    SDL_Vulkan_DestroySurface(vk_instance, vk_surface, NULL);
    vkDestroyInstance(vk_instance, NULL);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
