#version 450
layout(push_constant) uniform Push {
    mat4  view_proj;
    vec4  light_dir;
    vec4  light_color;
    vec4  ambient;
    /* x, y: how many times each detail map repeats across the base map.
       z: ShaderModelDetailMask -- which multipurpose channel gates the
       detail, 0 for none. */
    vec4  detail;
} push;

layout(set = 0, binding = 0) uniform sampler2D u_base;
layout(set = 0, binding = 1) uniform sampler2D u_light;
layout(set = 0, binding = 2) uniform sampler2D u_detail;
layout(set = 0, binding = 3) uniform sampler2D u_detail2;
layout(set = 0, binding = 4) uniform sampler2D u_multi;

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_world;
layout(location = 3) in vec2 v_lm_uv;
layout(location = 0) out vec4 out_color;

/* ShaderColorFunctionType, folding the running colour with the next map. */
vec4 fold(vec4 cur, vec4 nxt, int op) {
    if (op == 1)  return nxt;
    if (op == 2)  return cur * nxt;
    if (op == 3)  return cur * nxt * 2.0;
    if (op == 4)  return cur + nxt;
    if (op == 5 || op == 6) return cur + nxt - 0.5;
    if (op == 7)  return cur - nxt;
    if (op == 8)  return nxt - cur;
    if (op == 9)  return mix(nxt, cur, cur.a);
    if (op == 10) return mix(cur, nxt, cur.a);
    if (op == 11) return mix(cur, nxt, nxt.a);
    if (op == 12) return mix(nxt, cur, nxt.a);
    return cur;
}

void main() {
    /* A chicago layer (skies): up to three maps, each at its own repeat,
     * folded by the shader's colour and alpha functions. The scales ride in
     * detail (u) and light_dir (v), the functions in ambient. */
    if (push.light_color.w > 1.5) {
        int n = int(push.detail.w + 0.5);
        vec4 m0 = texture(u_base, v_uv * vec2(push.detail.x, push.light_dir.x));
        vec4 acc = m0;
        if (n > 1) {
            vec4 m1 = texture(u_detail, v_uv * vec2(push.detail.y, push.light_dir.y));
            acc = vec4(fold(acc, m1, int(push.ambient.x + 0.5)).rgb,
                       fold(acc, m1, int(push.ambient.z + 0.5)).a);
            if (n > 2) {
                vec4 m2 = texture(u_detail2, v_uv * vec2(push.detail.z, push.light_dir.z));
                acc = vec4(fold(acc, m2, int(push.ambient.y + 0.5)).rgb,
                           fold(acc, m2, int(push.ambient.w + 0.5)).a);
            }
        }
        out_color = clamp(acc, 0.0, 1.0);
        return;
    }
    vec4 base = texture(u_base, v_uv);
    /* Vertex colour (contrails): the colour rides in the normal and the
     * alpha in the lightmap U, both unused by anything unlit. A contrail
     * point changes colour and fades as it ages, and a texture per step
     * would be a texture per frame. Additive draws ignore alpha, so it is
     * folded into the colour as well. */
    if (push.detail.w > 2.5 && push.light_color.w < 1.5) {
        float a = clamp(v_lm_uv.x, 0.0, 1.0);
        out_color = vec4(base.rgb * v_normal * a, base.a * a);
        return;
    }
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

    /* A model shader can gate its detail by one channel of the
     * multipurpose map: R auxiliary, G self-illumination, B change colour,
     * A reflection, each available straight or inverted. Blood Gulch only
     * uses the reflection pair -- vehicles keep their detail off the shiny
     * panels. Masked out means neutral grey, which the double-biased
     * multiply below turns into "leave the base alone". */
    int dm = int(push.detail.z + 0.5);
    if (dm > 0) {
        vec4 mp = texture(u_multi, v_uv);
        float m = (dm <= 2) ? mp.a : (dm <= 4) ? mp.g : (dm <= 6) ? mp.b : mp.r;
        if ((dm & 1) == 1) m = 1.0 - m;        /* the odd codes are inverses */
        det = mix(vec3(0.5), det, m);
    }

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
    /* The alpha pipeline blends with SRC_ALPHA, so the texture's own alpha
     * has to reach it. Hardcoding 1.0 here meant every alpha-blended
     * surface drew opaque -- a rocket's smoke came out as black squares,
     * because a smoke sprite is a soft shape in the alpha channel over a
     * black background. The additive pipeline blends ONE/ONE and does not
     * care either way. */
    out_color = vec4(col, base.a);
}
