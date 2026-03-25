#version 460 core
// ============================================================================
// Corruption Pass - Visual distortion mirroring audio corruption
// [AUDIO HOOK] u_corruption is the same value as
// AudioManager::GetCorruptionLevel(). Both visual and audio distortion
// are driven by CorruptionSystem::GetNormalized().
// ============================================================================

in vec2 v_texcoord;

uniform sampler2D u_scene;       // Lit scene buffer
uniform float u_corruption;      // 0.0 - 1.0
uniform float u_time;
uniform vec2 u_resolution;

out vec4 frag_color;

// Pseudo-random hash
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv = v_texcoord;

    if (u_corruption < 0.01) {
        frag_color = texture(u_scene, uv);
        return;
    }

    // Stage 1 (0.0 - 0.25): Subtle UV wobble
    if (u_corruption > 0.0) {
        float wobble = sin(uv.y * 30.0 + u_time * 3.0) * u_corruption * 0.003;
        uv.x += wobble;
    }

    // Stage 2 (0.25 - 0.50): Chromatic aberration
    vec3 color;
    if (u_corruption > 0.25) {
        float offset = (u_corruption - 0.25) * 0.01;
        color.r = texture(u_scene, uv + vec2(offset, 0.0)).r;
        color.g = texture(u_scene, uv).g;
        color.b = texture(u_scene, uv - vec2(offset, 0.0)).b;
    } else {
        color = texture(u_scene, uv).rgb;
    }

    // Stage 3 (0.50 - 0.75): Static/noise overlay
    if (u_corruption > 0.50) {
        float noise_intensity = (u_corruption - 0.50) * 0.4;
        float noise = hash(uv * u_resolution + u_time * 100.0);
        // Intermittent bursts (matches audio static bursts)
        float burst = step(0.97, hash(vec2(floor(u_time * 4.0), 0.0)));
        color = mix(color, vec3(noise), noise_intensity * burst);
    }

    // Stage 4 (0.75 - 1.0): Screen tear + vignette
    if (u_corruption > 0.75) {
        float tear_intensity = (u_corruption - 0.75) * 4.0;
        // Horizontal tear lines
        float tear = step(0.98, hash(vec2(floor(uv.y * 50.0 + u_time * 5.0), u_time)));
        if (tear > 0.0) {
            uv.x += tear_intensity * 0.05 * sign(hash(vec2(u_time)) - 0.5);
            color = texture(u_scene, uv).rgb;
        }

        // Dark vignette closing in
        float vignette = 1.0 - length(v_texcoord - 0.5) * tear_intensity * 0.8;
        color *= max(vignette, 0.3);
    }

    // Desaturation increases with corruption
    float gray = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(color, vec3(gray), u_corruption * 0.4);

    frag_color = vec4(color, 1.0);
}
