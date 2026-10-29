#version 440

// The Genie's picture: what was in the panel, fading only as it enters the
// sphere and in the last moment of the trip.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 1) in float entered;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 from;
    vec4 sphere;
    float progress;
};

layout(binding = 1) uniform sampler2D picture;

void main()
{
    float tail = 1.0 - smoothstep(0.82, 1.0, progress);
    float inside = 1.0 - entered * 0.85;
    fragColor = texture(picture, qt_TexCoord0) * (qt_Opacity * tail * inside);
}
