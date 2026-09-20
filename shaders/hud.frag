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
    /* Meters only: the colour of the part the fill has not reached. */
    vec4  empty;
} push;

layout(set = 0, binding = 0) uniform sampler2D u_base;
layout(set = 0, binding = 1) uniform sampler2D u_light;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 t = texture(u_base, v_uv);
    if (push.ambient.y > 0.5) {
        if (t.a <= 0.004) discard;            /* outside the bar */
        /* Alpha is WHERE a pixel sits along the meter. Halo does not DROP
         * the part the fill has not reached -- it paints it in the
         * element's empty colour. The assault rifle needs that: its meter
         * minimum and maximum colours are the same blue, so the only thing
         * distinguishing a spent pip from a live one is that empty colour.
         * Discarding instead made a full magazine and an empty one differ
         * only in which pips were missing, and the art's rising per-row
         * alpha made the wrong ones go. */
        bool filled = t.a <= push.ambient.x;
        /* Past the threshold a pixel is simply LIT, and its opacity is the
         * art's own brightness -- not its alpha.
         *
         * Alpha here is the fill ramp and nothing else. Using it for
         * opacity as well made the assault rifle's pip grid lie: its three
         * rows carry rising alpha (about 0.2, 0.4, 0.8) because that is
         * their firing ORDER, so the early pips drew faint and the last
         * ones drew bright. A nearly empty magazine kept only the brightest
         * rows and so looked fuller than a full one.
         *
         * Brightness is the right channel for opacity because this art is
         * white-on-black and antialiases its shapes in RGB, not alpha: a
         * pip's edge pixels are dark but carry its full alpha. Weighting by
         * brightness softens those edges and makes the black between pips
         * disappear, which is also what stops the health bar's dim left cap
         * rendering as a black blob. */
        float lit = max(t.r, max(t.g, t.b));
        vec3 col = filled ? push.tint.rgb
                          : (push.empty.a > 0.0 ? push.empty.rgb : push.tint.rgb);
        float opacity = lit * (filled || push.empty.a > 0.0 ? 1.0 : 0.0);
        if (opacity <= 0.0) discard;
        out_color = vec4(col, opacity * push.tint.a);
    } else if (push.ambient.z > 0.5) {
        /* The art is a mask: its shape, the tag's colour. */
        out_color = vec4(push.tint.rgb, t.a * push.tint.a);
    } else {
        out_color = vec4(t.rgb * push.tint.rgb, t.a * push.tint.a);
    }
}
