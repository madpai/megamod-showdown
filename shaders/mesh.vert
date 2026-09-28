#version 450
layout(push_constant) uniform Push {
    mat4  view_proj;
    vec4  light_dir;
    vec4  light_color;
    vec4  ambient;
} push;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec2 in_lm_uv;

struct LocalLight { vec4 position_range; vec4 color_intensity; vec4 direction_inner; vec4 outer; };
layout(set = 1, binding = 0) uniform Frame {
    vec4 fog_color, fog, misc;
    mat4 inv_view_proj;
    vec4 visual, eye;
    LocalLight lights[8];
} frame;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_world;
layout(location = 3) out vec2 v_lm_uv;

void main() {
    gl_Position = push.view_proj * vec4(in_pos, 1.0);
    v_normal = in_normal;
    v_uv     = in_uv;
    /* Rigid model transforms are already folded into push.view_proj. Undo
     * only the camera transform, so every vertex gives the lighting shader
     * a true world position without expanding the 128-byte push range. */
    if (frame.visual.x > 0.5) {
        vec4 world = frame.inv_view_proj * gl_Position;
        v_world = world.xyz / world.w;
    } else v_world = in_pos;
    v_lm_uv  = in_lm_uv;
}
