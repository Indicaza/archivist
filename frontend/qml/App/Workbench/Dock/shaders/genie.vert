#version 440

// The Genie: a picture of a panel funnelling into a sphere in the Gravity
// Well (progress 0 -> 1), or out of it (1 -> 0). The mesh is a grid over
// the picture; every vertex is placed from its texture coordinate alone,
// so the item can cover the whole layer.
//
// Two movements overlap: the panel first bends, its sides curving in
// toward the sphere (nearest rows most); then it slides down that funnel,
// the edge nearest the sphere entering first and the far edge last.

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;

layout(location = 0) out vec2 qt_TexCoord0;
layout(location = 1) out float entered;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // The panel: x, y, width, height.
    vec4 from;
    // The sphere: centre x, centre y, radius.
    vec4 sphere;
    // 0 is the panel, 1 is inside the sphere.
    float progress;
};

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    vec2 uv = qt_MultiTexCoord0;
    qt_TexCoord0 = uv;

    float top = from.y;
    float bottom = from.y + from.w;
    bool down = sphere.y >= (top + bottom) * 0.5;
    // The edge furthest from the sphere, and the way from it to the sphere.
    float far = down ? top : bottom;
    float path = sphere.y - far;
    if (abs(path) < 1.0) {
        path = down ? 1.0 : -1.0;
    }

    float bend = smoothstep(0.0, 0.45, progress);
    float slide = clamp((progress - 0.16) / 0.84, 0.0, 1.0);
    // Pulled harder as it goes.
    slide = slide * slide * (1.6 - 0.6 * slide);

    // Each row: 0 at the far edge, 1 at the edge nearest the sphere.
    float frac = down ? uv.y : 1.0 - uv.y;
    float toward = path > 0.0 ? 1.0 : -1.0;
    // The Well floats over the panels, so a panel's near edge can lie past
    // the sphere: as it bends, the panel eases to fit before the sphere
    // (only when it has to), so every row comes into the funnel in order.
    float span = min(from.w, abs(path) * 0.85);
    float fitted = far + toward * frac * span;
    float travelled = fitted + slide * path;
    // Rows that reach the sphere go in and gather at its centre.
    float funnelled = down ? min(travelled, sphere.y) : max(travelled, sphere.y);
    float over = down ? travelled - sphere.y : sphere.y - travelled;
    entered = clamp(over / max(1.0, sphere.z * 1.5), 0.0, 1.0);

    float rest = top + uv.y * from.w;
    float y = mix(rest, funnelled, bend);

    // The funnel: full width at the far edge, the sphere's neck at the
    // sphere, curving between (a smooth S, steepest midway).
    float along = clamp((funnelled - far) / path, 0.0, 1.0);
    float curve = along * along * (3.0 - 2.0 * along);
    float neck = sphere.z * 0.55;
    float left = mix(from.x, sphere.x - neck, curve);
    float right = mix(from.x + from.z, sphere.x + neck, curve);
    left = mix(from.x, left, bend);
    right = mix(from.x + from.z, right, bend);

    float x = mix(left, right, uv.x);
    gl_Position = qt_Matrix * vec4(x, y, 0.0, 1.0);
}
