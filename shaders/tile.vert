#version 450 core

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

out vec2 v_uv;

uniform mat4 u_mvp;

void main() {
    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
