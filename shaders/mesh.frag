#version 450
layout(push_constant) uniform Push {
    mat4  view_proj;
    vec4  light_dir;
    vec4  light_color;
    vec4  ambient;
    /* x, y: how many times each detail map repeats across the base map.
       0 means this surface has none. */
    vec4  detail;
} push;

layout(set = 0, binding = 0) uniform sampler2D u_base;
layout(set = 0, binding = 1) uniform sampler2D u_light;
layout(set = 0, binding = 2) uniform sampler2D u_detail;
layout(set = 0, binding = 3) uniform sampler2D u_detail2;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_world;
layout(location = 3) in vec2 v_lm_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 base = texture(u_base, v_uv);
    vec3 albedo = base.rgb;
    vec3 lm     = texture(u_light, v_lm_uv).rgb;

    /* Halo layers detail maps over the base at a much higher frequency and
     * combines them as a double-biased multiply: grey leaves the base
     * alone, lighter and darker push it either way. Without this the ground
     * is one flat repeat of a low-resolution texture.
     *
     * There are TWO, blended by the base map's own alpha -- Blood Gulch's
     * ground is sand at 100x where that alpha is high and grass at 60x
     * where it is low, out of a single shader. The fallback texture is
     * mid-grey, so a surface with no detail map passes through untouched
     * and needs no branch.
     *
     * The fade: these tile up to a hundred times across one surface and we
     * upload no mipmaps, so at a distance the detail aliases into static.
     * Measuring how fast the detail UV moves per pixel and easing back to
     * neutral grey when it goes sub-pixel is what a mip chain would do. */
    vec2 uv1 = v_uv * max(push.detail.x, 1.0);
    vec2 uv2 = v_uv * max(push.detail.y, 1.0);
    vec3 d1 = texture(u_detail,  uv1).rgb;
    vec3 d2 = texture(u_detail2, uv2).rgb;
    vec3 det = (push.detail.y > 0.0) ? mix(d2, d1, base.a) : d1;

    float px = max(max(length(dFdx(uv1)), length(dFdy(uv1))),
                   max(length(dFdx(uv2)), length(dFdy(uv2))));
    float sharp = clamp(1.0 - px * 1.5, 0.0, 1.0);
    det = mix(vec3(0.5), det, sharp);

    albedo *= det * 2.0;

    /* Halo lightmaps are stored at half-bright and multiplied by 2 at display. */
    vec3 col = albedo * lm * 2.0;
    out_color = vec4(col, 1.0);
}
