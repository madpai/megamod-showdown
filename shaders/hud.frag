#version 450
/* Halo's HUD art is white with an alpha mask; the colour comes from the tag,
 * so tint here rather than baking a texture per colour.
 *
 * Meters are different: their alpha channel is a FILL RAMP, brightest where
 * the bar empties last, and the engine lights a pixel once the meter passes
 * it. The bar is then drawn flat in the tag's colour, not in the art's.
 *
 * ambient.x is the fill 0..1; ambient.y > 0.5 selects meter mode.
 */
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
    if (push.ambient.y > 0.5) {
        if (t.a <= 0.004) discard;                 /* outside the bar */
        if (t.a < 1.0 - push.ambient.x) discard;   /* past the fill */
        out_color = vec4(push.tint.rgb, push.tint.a);
    } else {
        out_color = vec4(t.rgb * push.tint.rgb, t.a * push.tint.a);
    }
}
