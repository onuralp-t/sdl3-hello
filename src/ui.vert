#version 450

layout(push_constant) uniform PushConstants {
    vec2 screen_size;
} push_constants;

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    // Map screen pixel space [0, width], [0, height] to Vulkan NDC [-1, 1]
    vec2 ndc = (in_pos / push_constants.screen_size) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    out_color = in_color;
}
