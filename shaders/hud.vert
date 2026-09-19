#version 450
/* Screen-space overlay. Positions arrive already in clip space (x and y in
 * -1..1, z ignored), so the HUD needs no matrix and no camera. */
layout(push_constant) uniform Push {
    mat4  view_proj;
    vec4  light_dir;
    vec4  tint;
    vec4  ambient;
} push;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec2 in_lm_uv;

layout(location = 0) out vec2 v_uv;

void main() {
    gl_Position = vec4(in_pos.x, in_pos.y, 0.0, 1.0);
    v_uv = in_uv;
}
