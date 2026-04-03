#version 450 core

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_texture;
uniform float u_opacity;

void main() {
    frag_color = texture(u_texture, v_uv) * vec4(1.0, 1.0, 1.0, u_opacity);
}
