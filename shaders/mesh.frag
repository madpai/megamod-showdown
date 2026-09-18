#version 450
layout(push_constant) uniform Push {
    mat4  view_proj;
    vec4  light_dir;
    vec4  light_color;
    vec4  ambient;
} push;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_world;
layout(location = 0) out vec4 out_color;

void main() {
    /* Placeholder shading until real textures land: BSP lighting values plus a
     * faint world-space grid so terrain shape is readable without materials. */
    vec3 n = normalize(length(v_normal) > 0.0001 ? v_normal : vec3(0.0, 0.0, 1.0));
    float ndl = max(dot(n, -normalize(push.light_dir.xyz)), 0.0);

    vec3 base = vec3(0.62, 0.60, 0.55);
    vec3 lit  = push.ambient.rgb + push.light_color.rgb * ndl;
    vec3 col  = base * clamp(lit, 0.0, 2.0);

    /* subtle contour lines every world unit to make slopes legible */
    vec3 g = abs(fract(v_world - 0.5) - 0.5) / max(fwidth(v_world), vec3(1e-4));
    float grid = 1.0 - min(min(g.x, g.y), 1.0);
    col = mix(col, col * 0.75, grid * 0.35);

    /* tint slightly by facing so flat terrain still reads as 3D */
    col *= 0.85 + 0.15 * n.z;

    out_color = vec4(col, 1.0);
}
