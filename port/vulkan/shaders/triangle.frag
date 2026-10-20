#version 450
layout(location=0) in vec4 color;
layout(location=1) in vec2 uv;
layout(set=0,binding=0) uniform sampler2D image;
layout(push_constant) uniform Draw { float alpha_cutoff; } draw;
layout(location=0) out vec4 outputColor;
void main() {
    vec4 shaded=color*texture(image,uv);
    if (shaded.a < draw.alpha_cutoff) discard;
    outputColor=shaded;
}
