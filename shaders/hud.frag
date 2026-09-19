#version 450
/* Halo's HUD art is white with an alpha mask; the colour comes from the tag,
 * so tint here rather than baking a texture per colour. */
layout(push_constant) uniform Push {
    mat4  view_proj;
    vec4  light_dir;
    vec4  tint;
    vec4  ambient;
} push;

layout(set = 0, binding = 0) uniform sampler2D u_base;
layout(set = 0, binding = 1) uniform sampler2D u_light;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 t = texture(u_base, v_uv);
    out_color = vec4(t.rgb * push.tint.rgb, t.a * push.tint.a);
}
