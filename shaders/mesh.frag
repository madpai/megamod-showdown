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
     * These tile up to a hundred times across one surface, so they live or
     * die on mipmapping: the textures carry a full chain and the sampler is
     * trilinear plus anisotropic. Before that they aliased into static and
     * needed a hand-rolled sub-pixel fade here, which is gone. */
    vec2 uv1 = v_uv * max(push.detail.x, 1.0);
    vec2 uv2 = v_uv * max(push.detail.y, 1.0);
    vec3 d1 = texture(u_detail,  uv1).rgb;
    vec3 d2 = texture(u_detail2, uv2).rgb;
    vec3 det = (push.detail.y > 0.0) ? mix(d2, d1, base.a) : d1;

    albedo *= det * 2.0;

    /* Halo lightmaps are stored at half-bright and multiplied by 2 at
     * display. The world carries one per surface.
     *
     * The first-person weapon does NOT: it has no lightmap, so it was
     * drawn at raw albedo with no lighting of any kind. The Trial's gun
     * textures are dark -- the assault rifle's body averages 58/255 -- so
     * unlit it came out nearly black, while the real game shows a lit
     * mid-grey rifle. Light it from the scene instead, on the same
     * half-bright convention the lightmaps use, so a weapon sits at the
     * same exposure as the ground it is standing on.
     *
     * light_color.w marks that path; the light direction arrives already
     * rotated into the viewmodel's own space, because its normals are
     * never transformed out of it. */
    vec3 col;
    if (push.light_color.w > 0.5) {
        /* WRAPPED, not clamped. A hard N.L splits the weapon into a blown
         * highlight and a black underside, which is not how Halo's gun
         * reads: it is evenly lit with soft modelling. Wrapping keeps the
         * whole model above the ambient floor and only varies the amount.
         * The x2 is the same half-bright convention the lightmaps use, and
         * is what brings a 58/255 gun texture up to the mid-grey the real
         * game shows. */
        float ndl = dot(normalize(v_normal), -push.light_dir.xyz) * 0.5 + 0.5;
        col = albedo * (push.ambient.rgb + push.light_color.rgb * ndl) * 2.0;
    } else {
        col = albedo * lm * 2.0;
    }
    out_color = vec4(col, 1.0);
}
