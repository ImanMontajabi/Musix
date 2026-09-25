#version 440
// Pixel rain: columns of square pixels falling. Each column belongs to a band
// of the spectrum, bass on the left and treble on the right, and how many of
// that band's columns fall, how bright and how long their trails are follows
// the band. A beat shows only in the drops: for a moment the bass columns
// carry more of them, brighter. Everything is a function of the uniforms, so
// a still frame costs nothing to hold.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float flow;         // rows fallen so far; integrated outside, so a change of speed never jumps
    float cell;         // size of one pixel, in item units
    float burst;        // a beat, 0..1, fading over a quarter of a second
    vec2 size;          // item size, in item units
    vec4 s0; vec4 s1; vec4 s2; vec4 s3; // the sixteen bands, 0..1
    vec4 g0; vec4 g1; vec4 g2; vec4 g3; vec4 g4; vec4 g5; // guarded areas: x, y, w, h as fractions
    vec4 body;          // the trail's colour
    vec4 cover;         // a second trail colour, from the cover
    vec4 head;          // the leading pixel's colour
};

float hash(float n) { return fract(sin(n * 127.1) * 43758.5453); }

float band(float x) {
    int b = int(clamp(floor(x * 16.0), 0.0, 15.0));
    vec4 v = b < 4 ? s0 : b < 8 ? s1 : b < 12 ? s2 : s3;
    return v[b % 4];
}

// 1 inside a guarded area, easing to 0 over a short margin around it.
float guardOf(vec4 g, vec2 uv) {
    if (g.z <= 0.0 || g.w <= 0.0)
        return 0.0;
    vec2 margin = vec2(0.02, 0.03);
    vec2 inside = smoothstep(g.xy - margin, g.xy, uv) * (1.0 - smoothstep(g.xy + g.zw, g.xy + g.zw + margin, uv));
    return inside.x * inside.y;
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 p = uv * size;
    vec2 id = floor(p / cell);
    vec2 f = fract(p / cell);
    float rows = max(1.0, size.y / cell);
    float v = band((id.x * cell + 0.5 * cell) / size.x);
    float h = hash(id.x);
    // Each column falls at its own steady speed; the music changes how many
    // fall and how they look, never where one is, so nothing jumps.
    float speed = 0.55 + 0.9 * h;
    float period = rows * (1.3 + 1.7 * hash(id.x + 7.0));
    float travelled = flow * speed + h * period;
    float cycle = floor(travelled / period);
    float d = mod(travelled, period) - id.y;
    // The bass columns answer a beat with more drops and brighter heads,
    // strongest at the far left and gone by a quarter of the way across.
    float bassSide = 1.0 - smoothstep(0.0, 0.25, (id.x * cell + 0.5 * cell) / size.x);
    float kick = burst * bassSide;
    float lit = step(hash(id.x * 1.37 + cycle * 3.1), 0.05 + 0.9 * v + 0.5 * kick);
    float trail = 3.0 + 18.0 * v;
    float inTrail = step(0.0, d) * (1.0 - clamp(d / trail, 0.0, 1.0));
    float leading = step(0.0, d) * (1.0 - clamp(d / 1.5, 0.0, 1.0));
    // A pixel is a square with a gap around it, so the grid reads as pixels.
    float square = step(0.14, f.x) * step(f.x, 0.86) * step(0.14, f.y) * step(f.y, 0.86);
    vec3 c = mix(body.rgb, cover.rgb, hash(id.x + 3.0));
    c = mix(c, head.rgb, clamp(leading * (0.3 + 0.6 * v + 0.4 * kick), 0.0, 1.0));
    float alpha = square * lit * (inTrail * (0.25 + 0.55 * v + 0.2 * kick) + leading * (0.35 + 0.5 * v + 0.4 * kick));
    // Behind text and controls the rain all but disappears, so they keep
    // their contrast; everywhere else it can be bright.
    float guarded = max(max(max(guardOf(g0, uv), guardOf(g1, uv)), max(guardOf(g2, uv), guardOf(g3, uv))),
                        max(guardOf(g4, uv), guardOf(g5, uv)));
    alpha = min(alpha, 0.9) * mix(1.0, 0.15, guarded);
    fragColor = vec4(c * alpha, alpha) * qt_Opacity;
}
