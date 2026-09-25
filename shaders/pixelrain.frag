#version 440
// Pixel rain: columns of square pixels falling, each column at its own pace.
// Everything here is a function of the uniforms, so a still frame is simply
// the same uniforms again and costs nothing to hold.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float flow;   // rows fallen so far; integrated outside, so a change of speed never jumps
    float level;  // overall loudness, 0..1
    float burst;  // a bass hit, 0..1, already limited to three a second
    float cell;   // size of one pixel, in item units
    vec2 size;    // item size, in item units
    vec4 body;    // the trail's colour
    vec4 cover;   // a second trail colour, from the cover
    vec4 head;    // the leading pixel's colour
};

float hash(float n) { return fract(sin(n * 127.1) * 43758.5453); }

void main() {
    vec2 p = qt_TexCoord0 * size;
    vec2 id = floor(p / cell);
    vec2 f = fract(p / cell);
    float rows = max(1.0, size.y / cell);
    float h = hash(id.x);
    // Each column falls at its own speed and restarts after its own gap.
    float speed = 0.55 + 0.9 * h;
    float period = rows * (1.3 + 1.7 * hash(id.x + 7.0));
    float travelled = flow * speed + h * period;
    float cycle = floor(travelled / period);
    float d = mod(travelled, period) - id.y;
    // Louder music lights more columns, and longer trails.
    float density = 0.18 + 0.62 * level;
    float lit = step(hash(id.x * 1.37 + cycle * 3.1), density);
    float trail = 4.0 + 16.0 * level;
    float inTrail = step(0.0, d) * (1.0 - clamp(d / trail, 0.0, 1.0));
    float leading = step(0.0, d) * (1.0 - clamp(d / 1.5, 0.0, 1.0));
    // A pixel is a square with a gap around it, so the grid reads as pixels.
    float square = step(0.14, f.x) * step(f.x, 0.86) * step(0.14, f.y) * step(f.y, 0.86);
    vec3 c = mix(body.rgb, cover.rgb, hash(id.x + 3.0));
    c = mix(c, head.rgb, leading * (0.35 + 0.45 * burst));
    float alpha = square * lit * (inTrail * (0.3 + 0.35 * level) + leading * (0.3 + 0.4 * burst));
    // Capped so the brightest pixel still leaves body text 4.5:1 against it.
    alpha = min(alpha, 0.6);
    fragColor = vec4(c * alpha, alpha) * qt_Opacity;
}
