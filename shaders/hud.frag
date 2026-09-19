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
        /* Keep the art's shading: its RGB carries the bar's gradient and
         * bright edge, and a flat tag colour turns Halo's shield into a
         * plain slab.
         *
         * Opacity cannot simply be that alpha, because alpha is already
         * doing duty as the fill ramp -- the shield's sits in a narrow band
         * around 0.45 and the health bar steps once per chevron, so using it
         * directly leaves the whole bar half transparent. Halo separates the
         * two with the meter's alpha multiplier, alpha bias and min alpha,
         * which we do not read yet; until we do, a curve that makes the bar
         * solid while leaving the art's faintest marks faint stands in for
         * them. Without it the health bar's dim left cap turns into a black
         * blob. */
        /* HUD meter art is white-on-black: the gaps between the assault
         * rifle's pips are pure black at a low but non-zero alpha, and drawn
         * they outline every pip in dark. Black in this art means "nothing
         * here", so gate on the art's own brightness. */
        /* HUD meter art is white-on-black, and its shape is antialiased in
         * the RGB rather than the alpha -- a pip's edge pixels are dark but
         * carry the same alpha as its bright middle. Drawn at full opacity
         * that outlines every pip in navy. Weighting opacity by the art's
         * own brightness composites it the way white-on-black art expects:
         * the edges go soft, the black between pips disappears. */
        float lit = max(t.r, max(t.g, t.b));
        float opacity = smoothstep(0.15, 0.40, t.a) * lit;
        out_color = vec4(t.rgb * push.tint.rgb, opacity * push.tint.a);
    } else if (push.ambient.z > 0.5) {
        /* The art is a mask: its shape, the tag's colour. */
        out_color = vec4(push.tint.rgb, t.a * push.tint.a);
    } else {
        out_color = vec4(t.rgb * push.tint.rgb, t.a * push.tint.a);
    }
}
