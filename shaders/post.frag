#version 450
/* Post-processing. One program, four jobs, picked by mode.z:
 *   0  final: upscale the scene, FXAA/sharpen, bloom, exposure, tonemap,
 *      grade, vignette, dither -- written to the screen
 *   1  bloom bright pass: what is over the threshold, downsampled 4x
 *   2  bloom blur, horizontal
 *   3  bloom blur, vertical
 *
 * The scene target can be larger than what was drawn into it: dynamic
 * resolution renders into its top-left corner, so every lookup maps its uv
 * through src.xy (used / allocated) and clamps inside the used area. */
layout(push_constant) uniform Push {
    vec4 src;     /* xy: used fraction of the source; zw: 1 / source texels */
    vec4 grade;   /* exposure, contrast, saturation, warmth */
    vec4 fx;      /* bloom strength, vignette, sharpen, fxaa on */
    vec4 mode;    /* x tonemap, y bloom on, z pass, w bloom threshold */
    vec4 bloom;   /* xy: used fraction of the bloom target; zw: 1 / its texels */
} push;

layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(set = 0, binding = 1) uniform sampler2D u_bloom;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec3 src_at(vec2 uv) {
    vec2 lim = push.src.xy - push.src.zw * 0.5;
    return texture(u_src, clamp(uv, push.src.zw * 0.5, lim)).rgb;
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

/* Narkowicz's fit of the ACES filmic curve, applied in linear light: the
 * scene is authored in display space, so it goes out to linear, through
 * the curve, and back. */
vec3 aces(vec3 x) {
    x = max(x, vec3(0.0));
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    int pass = int(push.mode.z + 0.5);
    vec2 texel = push.src.zw;

    if (pass == 1) {
        /* 4x4 box via four bilinear taps, then a soft knee over the
         * threshold, so highlights fade in rather than pop. */
        vec2 uv = v_uv * push.src.xy;
        vec3 c = (src_at(uv + texel * vec2(-1.0, -1.0)) + src_at(uv + texel * vec2(1.0, -1.0)) +
                  src_at(uv + texel * vec2(-1.0,  1.0)) + src_at(uv + texel * vec2(1.0,  1.0))) * 0.25;
        float t = push.mode.w, knee = t * 0.5;
        float l = max(c.r, max(c.g, c.b));
        float soft = clamp(l - t + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 1e-4);
        float w = max(soft, l - t) / max(l, 1e-4);
        out_color = vec4(c * w, 1.0);
        return;
    }
    if (pass == 2 || pass == 3) {
        /* 9-tap Gaussian as five bilinear fetches. */
        vec2 dir = pass == 2 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
        vec2 uv = v_uv * push.src.xy;
        vec3 c = src_at(uv) * 0.2270270270;
        c += src_at(uv + dir * 1.3846153846) * 0.3162162162;
        c += src_at(uv - dir * 1.3846153846) * 0.3162162162;
        c += src_at(uv + dir * 3.2307692308) * 0.0702702703;
        c += src_at(uv - dir * 3.2307692308) * 0.0702702703;
        out_color = vec4(c, 1.0);
        return;
    }

    /* ---- final ---- */
    vec2 uv = v_uv * push.src.xy;
    vec3 c = src_at(uv);

    /* FXAA (a compact take on 3.11's idea): find the local edge from luma,
     * blur along it by how strong it is. Skipped on flat areas. */
    if (push.fx.w > 0.5 || push.fx.z > 0.0) {
        vec3 n = src_at(uv + vec2(0.0, -texel.y));
        vec3 s = src_at(uv + vec2(0.0,  texel.y));
        vec3 e = src_at(uv + vec2( texel.x, 0.0));
        vec3 w = src_at(uv + vec2(-texel.x, 0.0));
        float lc = luma(min(c, 1.0)), ln = luma(min(n, 1.0)), ls = luma(min(s, 1.0));
        float le = luma(min(e, 1.0)), lw = luma(min(w, 1.0));
        float lmin = min(lc, min(min(ln, ls), min(le, lw)));
        float lmax = max(lc, max(max(ln, ls), max(le, lw)));
        float range = lmax - lmin;
        if (push.fx.w > 0.5 && range > max(0.0312, lmax * 0.125)) {
            vec2 grad = vec2(lw - le, ln - ls);
            vec2 dir = vec2(-grad.y, grad.x);          /* along the edge */
            float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + max(range * 0.25, 1e-3));
            dir = clamp(dir * rcp, vec2(-4.0), vec2(4.0)) * texel;
            vec3 a = 0.5 * (src_at(uv - dir * (1.0 / 6.0)) + src_at(uv + dir * (1.0 / 6.0)));
            vec3 b = a * 0.5 + 0.25 * (src_at(uv - dir * 0.5) + src_at(uv + dir * 0.5));
            float lb = luma(min(b, 1.0));
            c = (lb < lmin || lb > lmax) ? a : b;
        }
        /* Contrast-adaptive sharpening: strong where the neighbourhood is
         * soft, weak where it is already contrasty, so upscaled frames
         * regain edges without ringing. */
        if (push.fx.z > 0.0) {
            float amt = push.fx.z * (1.0 - clamp(range * 2.0, 0.0, 1.0));
            vec3 blur = (n + s + e + w) * 0.25;
            c = max(c + (c - blur) * amt * 1.5, vec3(0.0));
        }
    }

    if (push.mode.y > 0.5) {
        vec2 buv = clamp(v_uv * push.bloom.xy, push.bloom.zw * 0.5, push.bloom.xy - push.bloom.zw * 0.5);
        c += texture(u_bloom, buv).rgb * push.fx.x;
    }

    c *= push.grade.x;
    if (push.mode.x > 0.5) {
        /* 0.72 puts mid-grey back where it was: the fit maps 0.13 to 0.18,
         * so without it every mid-tone -- sky, skin, ground -- came out
         * paler than the game's own look. Highlights still roll off. */
        c = pow(max(c, vec3(0.0)), vec3(2.2)) * 0.72;
        c = aces(c);
        c = pow(c, vec3(1.0 / 2.2));
    }
    c = clamp(c, 0.0, 1.0);

    /* Grade: contrast about mid-grey, saturation about luma, and warmth
     * as a gentle red/blue balance. */
    c = (c - 0.5) * push.grade.y + 0.5;
    c = mix(vec3(luma(c)), c, push.grade.z);
    c *= vec3(1.0 + 0.08 * push.grade.w, 1.0, 1.0 - 0.08 * push.grade.w);

    if (push.fx.y > 0.0) {
        vec2 d = v_uv - 0.5;
        c *= 1.0 - push.fx.y * smoothstep(0.35, 0.95, length(d) * 1.35);
    }

    /* Half-a-step dither kills banding in skies and fog. */
    float h = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (h - 0.5) / 255.0;
    out_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
