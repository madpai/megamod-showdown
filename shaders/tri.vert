#version 450
layout(push_constant) uniform Push { float spin; } push;
layout(location = 0) out vec3 vcolor;
void main() {
    vec2 p[3] = vec2[3](vec2(0.0, -0.6), vec2(0.55, 0.45), vec2(-0.55, 0.45));
    vec3 c[3] = vec3[3](vec3(1.0, 0.35, 0.2), vec3(0.3, 1.0, 0.45), vec3(0.35, 0.55, 1.0));
    float s = sin(push.spin), co = cos(push.spin);
    vec2 v = p[gl_VertexIndex];
    gl_Position = vec4(vec2(v.x * co - v.y * s, v.x * s + v.y * co), 0.0, 1.0);
    vcolor = c[gl_VertexIndex];
}
