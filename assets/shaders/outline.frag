#version 460 core
// ============================================================================
// Outline Pass — Sobel edge detection on normals + depth discontinuity
// Composites scene color with anime-style outlines.
// Enhanced: variable thickness, inner-line detection, glow-edge blending
// TUNED for new model with improved normals (50.6% outward)
// ============================================================================

in vec2 v_texcoord;

uniform sampler2D u_normal;          // G-Buffer normals
uniform sampler2D u_depth;           // G-Buffer position (pseudo-depth)
uniform sampler2D u_scene;           // Cel + corruption composited scene
uniform vec3 u_outline_color;        // Region-dependent outline color
uniform float u_thickness;           // Outline strength multiplier
uniform vec2 u_resolution;           // Screen resolution

out vec4 frag_color;

float sobel_normal(vec2 uv, vec2 texel) {
    vec3 tl = texture(u_normal, uv + vec2(-1,-1) * texel).rgb;
    vec3 t  = texture(u_normal, uv + vec2( 0,-1) * texel).rgb;
    vec3 tr = texture(u_normal, uv + vec2( 1,-1) * texel).rgb;
    vec3 l  = texture(u_normal, uv + vec2(-1, 0) * texel).rgb;
    vec3 r  = texture(u_normal, uv + vec2( 1, 0) * texel).rgb;
    vec3 bl = texture(u_normal, uv + vec2(-1, 1) * texel).rgb;
    vec3 b  = texture(u_normal, uv + vec2( 0, 1) * texel).rgb;
    vec3 br = texture(u_normal, uv + vec2( 1, 1) * texel).rgb;

    // Sobel X and Y
    vec3 gx = -tl - 2.0*l - bl + tr + 2.0*r + br;
    vec3 gy = -tl - 2.0*t - tr + bl + 2.0*b + br;

    return length(gx) + length(gy);
}

float sobel_depth(vec2 uv, vec2 texel) {
    float tl = length(texture(u_depth, uv + vec2(-1,-1) * texel).rgb);
    float t  = length(texture(u_depth, uv + vec2( 0,-1) * texel).rgb);
    float tr = length(texture(u_depth, uv + vec2( 1,-1) * texel).rgb);
    float l  = length(texture(u_depth, uv + vec2(-1, 0) * texel).rgb);
    float r  = length(texture(u_depth, uv + vec2( 1, 0) * texel).rgb);
    float bl = length(texture(u_depth, uv + vec2(-1, 1) * texel).rgb);
    float b  = length(texture(u_depth, uv + vec2( 0, 1) * texel).rgb);
    float br = length(texture(u_depth, uv + vec2( 1, 1) * texel).rgb);

    float gx = -tl - 2.0*l - bl + tr + 2.0*r + br;
    float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;
    return sqrt(gx*gx + gy*gy);
}

void main() {
    vec2 texel = 1.0 / u_resolution;
    vec3 scene_color = texture(u_scene, v_texcoord).rgb;

    // Edge detection
    // NOTE: TRELLIS AI-generated models have noisy normals.
    // Thresholds tuned for new model with improved normals:
    //   - Slightly more sensitive to catch real silhouette edges
    //   - Still high enough to avoid per-pixel noise outlines
    float normal_edge = sobel_normal(v_texcoord, texel) * u_thickness;
    float depth_edge = sobel_depth(v_texcoord, texel) * u_thickness * 0.5;

    // Outer edge: only at strong silhouette boundaries (tuned thresholds)
    float outer_edge = max(normal_edge * 0.45, depth_edge);
    outer_edge = smoothstep(0.10, 0.28, outer_edge);

    // Inner edge: only at major feature boundaries (creases between limbs)
    float inner_edge = sobel_normal(v_texcoord, texel * 0.6) * u_thickness * 0.4;
    inner_edge = smoothstep(0.18, 0.38, inner_edge) * 0.35;

    float edge = max(outer_edge, inner_edge);

    // Softer outline: blend more toward scene color so outlines darken gently
    vec3 final_outline = mix(u_outline_color, scene_color * 0.5, 0.30);

    // Composite
    vec3 final_color = mix(scene_color, final_outline, edge);

    // Subtle film grain for atmosphere
    float grain = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    final_color += (grain - 0.5) * 0.012;

    frag_color = vec4(final_color, 1.0);
}
