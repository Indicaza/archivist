#version 440

// The Genie's shadow: the warped picture's outline in black, drawn under it
// (twice, offset) so the picture lifts off the panels it flies over.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 1) in float entered;
layout(location = 2) in float drawn;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 from;
    vec4 sphere;
    float progress;
    vec4 glow;
};

layout(binding = 1) uniform sampler2D picture;

void main()
{
    float tail = 1.0 - smoothstep(0.7, 0.95, progress);
    float alpha = texture(picture, qt_TexCoord0).a * (1.0 - entered) * tail;
    fragColor = vec4(0.0, 0.0, 0.0, alpha) * qt_Opacity;
}
