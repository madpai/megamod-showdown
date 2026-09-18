#version 450
layout(push_constant) uniform Push {
    mat4  view_proj;
    vec4  light_dir;
    vec4  light_color;
    vec4  ambient;
} push;

layout(set = 0, binding = 0) uniform sampler2D u_base;
layout(set = 0, binding = 1) uniform sampler2D u_light;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_world;
layout(location = 3) in vec2 v_lm_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 albedo = texture(u_base, v_uv).rgb;
    vec3 lm     = texture(u_light, v_lm_uv).rgb;
    /* Halo lightmaps are stored at half-bright and multiplied by 2 at display. */
    vec3 col = albedo * lm * 2.0;
    out_color = vec4(col, 1.0);
}
