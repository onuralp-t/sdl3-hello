#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_UI_VERTICES 65536

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
    /* Input state */
    float mouse_x;
    float mouse_y;
    bool mouse_down;
    bool mouse_pressed;
    bool mouse_released;

    /* Interaction state */
    UiId hot_id;
    UiId active_id;

    /* Geometry output */
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

    /* Two triangles forming a quad */
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

    /* Styling: normal, hovered, and pressed */
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

    /* Background bar */
    ui_draw_rect(ui_ctx, x, y, width, height, 0.15f, 0.15f, 0.18f, 1.0f);

    /* Filled active region */
    float fill_pct = (*value - min_val) / (max_val - min_val);
    if (fill_pct < 0.0f) fill_pct = 0.0f;
    if (fill_pct > 1.0f) fill_pct = 1.0f;
    ui_draw_rect(ui_ctx, x, y, width * fill_pct, height, 0.20f, 0.55f, 0.90f, 1.0f);

    return changed;
}

/* -------------------------------------------------------------------------- */
/* SDL3 & Vulkan Renderer Setup                                               */
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

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    const int initial_width = 1280;
    const int initial_height = 720;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return -1;
    }

    /* Create window with SDL_WINDOW_VULKAN */
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

    /* 1. SDL3 Vulkan Instance Extension Query */
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

    /* 2. SDL3 Cross-Platform Surface Creation */
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, vk_instance, NULL, &vk_surface)) {
        SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
        return -1;
    }

    /* 3. Pick Physical Device & Presentation Support */
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(vk_instance, &physical_device_count, NULL);
    VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * physical_device_count);
    vkEnumeratePhysicalDevices(vk_instance, &physical_device_count, physical_devices);

    VkPhysicalDevice physical_device = physical_devices[0];
    uint32_t graphics_queue_family = 0;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);
    VkQueueFamilyProperties *queue_families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            /* Check presentation support through SDL3 helper */
            if (SDL_Vulkan_GetPresentationSupport(vk_instance, physical_device, i)) {
                graphics_queue_family = i;
                break;
            }
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
    vkCreateDevice(physical_device, &device_info, NULL, &vk_device);

    VkQueue graphics_queue;
    vkGetDeviceQueue(vk_device, graphics_queue_family, 0, &graphics_queue);

    /* 5. Create Host-Visible Vertex Buffer for Immediate Mode UI */
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;
    VkDeviceSize buffer_size = sizeof(UiVertex) * MAX_UI_VERTICES;

    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCreateBuffer(vk_device, &buffer_create_info, NULL, &vertex_buffer);

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(vk_device, vertex_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(
            physical_device,
            mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ),
    };
    vkAllocateMemory(vk_device, &alloc_info, NULL, &vertex_buffer_memory);
    vkBindBufferMemory(vk_device, vertex_buffer, vertex_buffer_memory, 0);

    /* Map persistent pointer to upload UI vertex data without overhead */
    void *mapped_vertex_data = NULL;
    vkMapMemory(vk_device, vertex_buffer_memory, 0, buffer_size, 0, &mapped_vertex_data);

    /* 6. Push Constants Layout */
    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(UiPushConstants),
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VkPipelineLayout pipeline_layout;
    vkCreatePipelineLayout(vk_device, &pipeline_layout_info, NULL, &pipeline_layout);

    /* ---------------------------------------------------------------------- */
    /* Application / UI State Loop                                            */
    /* ---------------------------------------------------------------------- */
    UiContext ui_ctx;
    ui_init(&ui_ctx);

    bool running = true;
    int click_counter = 0;
    float volume_level = 0.5f;

    // bool // prev_mouse_down = false;

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
            }
        }

        float mouse_x, mouse_y;
        SDL_MouseButtonFlags mouse_state = SDL_GetMouseState(&mouse_x, &mouse_y);
        bool mouse_down = (mouse_state & SDL_BUTTON_LMASK) != 0;

        int window_width, window_height;
        SDL_GetWindowSizeInPixels(window, &window_width, &window_height);

        /* --- UI Definition Phase --- */
        ui_begin_frame(&ui_ctx, mouse_x, mouse_y, mouse_down, mouse_pressed, mouse_released);

        /* Draw a translucent background panel */
        ui_panel(&ui_ctx, 50.0f, 50.0f, 320.0f, 220.0f, 0.10f, 0.10f, 0.12f, 0.90f);

        /* Draw an interactive button */
        if (ui_button(&ui_ctx, "btn_increment", 70.0f, 80.0f, 150.0f, 40.0f)) {
            click_counter++;
            SDL_Log("Button Clicked! Total: %d", click_counter);
        }

        /* Draw an interactive slider */
        ui_slider(&ui_ctx, "sld_volume", 70.0f, 150.0f, 280.0f, 20.0f, &volume_level, 0.0f, 1.0f);

        /* --- Vulkan Stream Upload --- */
        if (ui_ctx.vertex_count > 0) {
            memcpy(mapped_vertex_data, ui_ctx.vertices, ui_ctx.vertex_count * sizeof(UiVertex));
        }

        /*
           --- Vulkan Recording Instructions (inside active RenderPass/CommandBuffer):

           UiPushConstants push_constants = {
               .screen_width = (float)window_width,
               .screen_height = (float)window_height,
           };

           vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline);
           vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UiPushConstants), &push_constants);

           VkDeviceSize offset = 0;
           vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
           vkCmdDraw(cmd, ui_ctx.vertex_count, 1, 0, 0);
        */

        // prev_mouse_down = mouse_down;
        SDL_Delay(16); /* ~60 FPS frame throttle */
    }

    /* Cleanup */
    vkUnmapMemory(vk_device, vertex_buffer_memory);
    vkDestroyBuffer(vk_device, vertex_buffer, NULL);
    vkFreeMemory(vk_device, vertex_buffer_memory, NULL);
    vkDestroyPipelineLayout(vk_device, pipeline_layout, NULL);
    vkDestroyDevice(vk_device, NULL);
    SDL_Vulkan_DestroySurface(vk_instance, vk_surface, NULL);
    vkDestroyInstance(vk_instance, NULL);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
