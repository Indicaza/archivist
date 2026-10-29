#version 440

// The Genie's picture: what was in the panel, fading only as it enters the
// sphere and in the last moment of the trip.

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
    float tail = 1.0 - smoothstep(0.86, 1.0, progress);
    float inside = 1.0 - entered * 0.8;
    vec4 colour = texture(picture, qt_TexCoord0);
    // Lit by the pull: brightest in the neck and as it goes in.
    float lit = glow.a * clamp(drawn * drawn * 0.9 + entered, 0.0, 1.0);
    colour.rgb = mix(colour.rgb, glow.rgb * colour.a, lit * 0.55) + glow.rgb * colour.a * lit * 0.25;
    fragColor = colour * (qt_Opacity * tail * inside);
}
