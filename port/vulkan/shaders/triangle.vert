#version 450
layout(location=0) in vec4 position;
layout(location=1) in vec4 vertex_color;
layout(location=2) in vec2 vertex_uv;
layout(location=0) out vec4 color;
layout(location=1) out vec2 uv;
void main() {
    gl_Position=position;
    color=vertex_color;
    uv=vertex_uv;
}
