#version 450
layout(push_constant) uniform Push {
    mat4  view_proj;    /* 0   .. 63  */
    vec4  light_dir;    /* 64  .. 79  (xyz used) */
    vec4  light_color;  /* 80  .. 95  */
    vec4  ambient;      /* 96  .. 111 */
} push;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_world;

void main() {
    gl_Position = push.view_proj * vec4(in_pos, 1.0);
    v_normal = in_normal;
    v_uv     = in_uv;
    v_world  = in_pos;
}
