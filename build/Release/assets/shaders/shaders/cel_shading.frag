#version 460 core
// ============================================================================
// Cel Shading Pass — Quantized toon lighting with:
//   - Shadow mapping (directional)
//   - Visible sun disc matching u_light_dir
//   - Distance fog (ash atmosphere)
//   - SSAO-style ambient darkening at creases (optimized for RTX 3050)
//   - Rim lighting (specular edge glow)
//   - Corruption color distortion
//   - Region-specific hue tinting
//   - Art style: dark, gritty, weathered (matching concept art)
// ============================================================================

in vec2 v_texcoord;

// G-Buffer inputs
uniform sampler2D u_position;
uniform sampler2D u_normal;
uniform sampler2D u_albedo;
uniform sampler2DShadow u_shadow_map;

// Lighting
uniform vec3 u_light_dir;       // Direction light points (toward ground)
uniform vec3 u_light_color;
uniform vec3 u_ambient_color;
uniform mat4 u_light_space;

// Camera
uniform vec3 u_camera_pos;

// Cel shading config
uniform int u_cel_steps;
uniform vec3 u_dominant_hue;

// Effects
uniform float u_corruption;
uniform float u_time;
uniform vec2 u_resolution;

out vec4 frag_color;

// ---- Pseudo-random ----
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// ---- Quantized cel shading with corruption modulation ----
float cel_shade(float NdotL, int steps) {
    float s = float(steps);
    float shaded = floor(NdotL * s + 0.5) / s;
    float corrupt_offset = sin(gl_FragCoord.x * 0.1 + u_time * 2.0) * u_corruption * 0.15;
    return clamp(shaded + corrupt_offset, 0.0, 1.0);
}

// ---- Shadow sampling with PCF 3x3 softening ----
float compute_shadow(vec3 world_pos) {
    vec4 light_clip = u_light_space * vec4(world_pos, 1.0);
    vec3 proj = light_clip.xyz / light_clip.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float shadow = 0.0;
    float texel = 1.0 / 1024.0; // Optimized: 1024 shadow map for RTX 3050
    float bias = 0.004;
    float ref = proj.z - bias;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            shadow += texture(u_shadow_map, vec3(proj.xy + vec2(x, y) * texel, ref));
        }
    }
    shadow /= 9.0;

    return shadow;
}

// ---- SSAO hint (4-tap optimized for RTX 3050) ----
// TRELLIS AI-generated models have noisy normals. We use wider sampling
// and thresholding to only detect real concavities, not per-vertex noise.
// New model has improved normals (50.6% outward), so we can be slightly
// more sensitive than before while still filtering noise.
float compute_ssao_hint(vec2 uv) {
    float texel = 1.5 / max(u_resolution.x, u_resolution.y);
    vec3 center_n = texture(u_normal, uv).rgb;
    float occlusion = 0.0;

    // 4-tap cross pattern — wider spacing to skip per-vertex noise
    vec2 offsets[4] = vec2[](
        vec2(-1, 0), vec2(1, 0),
        vec2(0, -1), vec2(0, 1)
    );

    for (int i = 0; i < 4; i++) {
        vec3 neighbor_n = texture(u_normal, uv + offsets[i] * texel * 3.5).rgb;
        float diff = 1.0 - max(dot(center_n, neighbor_n), 0.0);
        // Threshold: ignore noise, only count real crevices
        diff = smoothstep(0.25, 0.70, diff);
        occlusion += diff;
    }

    occlusion /= 4.0;
    return 1.0 - occlusion * 0.40;  // Slightly stronger than before — normals are cleaner
}

// ---- Ash sky rendering with visible sun disc ----
vec3 ash_sky(vec2 uv) {
    // Overcast sky — concept art: heavy gray-purple clouds across the ENTIRE sky
    // NOT a dark gradient — uniformly cloudy with slight warm tint near horizon
    vec3 horizon = vec3(0.45, 0.38, 0.33);  // Warm gray-brown at horizon (ash haze)
    vec3 mid_sky = vec3(0.50, 0.44, 0.48);  // Muted purple-gray midsky
    vec3 zenith  = vec3(0.46, 0.40, 0.46);  // Gray-purple at zenith (NOT darker than mid)

    float t = uv.y;
    vec3 sky = mix(horizon, mid_sky, smoothstep(0.15, 0.45, t));
    sky = mix(sky, zenith, smoothstep(0.5, 0.9, t));

    // === VISIBLE SUN DISC ===
    // Sun position: negate light direction and project onto screen-space hemisphere
    // u_light_dir points toward ground (0.3, -0.65, 0.4), so sun is at (-0.3, 0.65, -0.4)
    vec3 sun_dir = normalize(-u_light_dir);
    // Map sun direction to UV coordinates (approximate hemisphere projection)
    // X maps to horizontal, Y (up component) maps to vertical height in sky
    vec2 sun_uv = vec2(
        0.5 + sun_dir.x * 0.35,                   // Horizontal position
        0.3 + sun_dir.y * 0.55                     // Vertical — higher = more up
    );

    float sun_dist = distance(uv, sun_uv);

    // Inner sun disc — bright, hazy glow through ash clouds
    float sun_core = exp(-sun_dist * sun_dist * 60.0);
    vec3 sun_color = vec3(0.90, 0.60, 0.30);  // Warm orange, visible through ash
    sky += sun_color * sun_core * 1.2;

    // Outer glow halo — wide, warm, prominent
    float sun_halo = exp(-sun_dist * sun_dist * 5.0);
    vec3 halo_color = vec3(0.55, 0.38, 0.18);
    sky += halo_color * sun_halo * 0.7;

    // Light bloom rays (god rays hint)
    float ray_angle = atan(uv.y - sun_uv.y, uv.x - sun_uv.x);
    float rays = sin(ray_angle * 8.0 + u_time * 0.1) * 0.5 + 0.5;
    rays *= exp(-sun_dist * 3.5);
    sky += vec3(0.35, 0.22, 0.10) * rays * 0.25;

    // Animated ash/cloud streaks — thick, visible clouds matching concept art
    float streak1 = sin(uv.x * 25.0 + u_time * 0.2 + uv.y * 8.0) * 0.5 + 0.5;
    streak1 *= sin(uv.x * 12.0 - u_time * 0.12 + uv.y * 3.0) * 0.5 + 0.5;
    sky += vec3(0.08, 0.06, 0.06) * smoothstep(0.55, 0.85, streak1) * 0.8;

    // Second layer of thinner, faster-moving clouds
    float streak2 = sin(uv.x * 50.0 + u_time * 0.4 - uv.y * 5.0) * 0.5 + 0.5;
    sky += vec3(0.06, 0.04, 0.05) * smoothstep(0.65, 0.90, streak2) * 0.5;

    // Corruption desaturates sky (only at high corruption)
    if (u_corruption > 0.5) {
        float gray = dot(sky, vec3(0.299, 0.587, 0.114));
        sky = mix(sky, vec3(gray * 0.75), (u_corruption - 0.5) * 0.7);
    }

    return sky;
}

// ---- Distance fog (thick ash atmosphere — concept art style) ----
vec3 apply_fog(vec3 color, float distance_from_camera) {
    float fog_density = 0.014 + u_corruption * 0.010;
    float fog_factor = 1.0 - exp(-distance_from_camera * fog_density);
    fog_factor = clamp(fog_factor, 0.0, 0.75);

    vec3 fog_near = vec3(0.35, 0.28, 0.24);  // Warm ash-brown (visible)
    vec3 fog_far  = vec3(0.28, 0.22, 0.20);  // Cooler distant murk
    vec3 fog_color = mix(fog_near, fog_far, fog_factor);

    // Corruption shifts fog to sickly green
    if (u_corruption > 0.5) {
        float shift = (u_corruption - 0.5) * 0.5;
        fog_color = mix(fog_color, vec3(0.12, 0.16, 0.08), shift);
    }

    return mix(color, fog_color, fog_factor);
}

void main() {
    vec4 albedo = texture(u_albedo, v_texcoord);

    // Sky for empty pixels
    if (albedo.a < 0.01) {
        frag_color = vec4(ash_sky(v_texcoord), 1.0);
        return;
    }

    vec3 world_pos = texture(u_position, v_texcoord).rgb;
    vec3 normal = normalize(texture(u_normal, v_texcoord).rgb);
    vec3 light_dir = normalize(-u_light_dir);

    // === Base lighting (Half-Lambert, no squaring) ===
    // Standard NdotL clamps at 0, making 50%+ of TRELLIS surfaces pitch black.
    // Half-Lambert remaps [-1,1] → [0,1] so all surfaces get some light.
    // NO squaring — squaring pushes too many normals back to the 0-band.
    float raw_NdotL = dot(normal, light_dir);
    float NdotL = raw_NdotL * 0.5 + 0.5;  // Half-Lambert: range [0, 1]
    float cel = cel_shade(NdotL, u_cel_steps);
    // Minimum cel floor: even the darkest normals get diffuse light.
    // Without this, TRELLIS models with noisy normals lose all detail in shadow.
    // 0.25 ensures clear visibility even with 56% backward-facing normals.
    cel = max(cel, 0.25);

    // === Shadow ===
    float shadow = compute_shadow(world_pos);
    shadow = shadow < 0.5 ? 0.60 : 1.0; // Hard cel shadow boundary (raised floor)

    // === SSAO hint ===
    float ao = compute_ssao_hint(v_texcoord);

    // === Rim lighting (stronger for backlit objects to prevent silhouettes) ===
    // TRELLIS models with noisy normals need aggressive rim to break up the
    // dark silhouette and create visual interest on edges.
    vec3 view_dir = normalize(u_camera_pos - world_pos);
    float rim = 1.0 - max(dot(normal, view_dir), 0.0);
    float backlit = max(0.0, -dot(normal, light_dir)); // How much surface faces away from light
    rim = smoothstep(0.35, 0.90, rim) * (0.40 + backlit * 0.30); // Wider, stronger rim

    // === Specular highlight (cel-shaded hard cutoff) ===
    vec3 half_vec = normalize(light_dir + view_dir);
    float spec = pow(max(dot(normal, half_vec), 0.0), 32.0);
    spec = spec > 0.5 ? 0.25 : 0.0;

    // === Hemisphere ambient (sky bounce from above + ground bounce from below) ===
    // This prevents pure-black silhouettes on backlit faces
    float hemi = normal.y * 0.5 + 0.5; // 0=facing down, 1=facing up
    vec3 sky_ambient = u_ambient_color * 1.1;
    vec3 ground_ambient = u_ambient_color * vec3(0.8, 0.7, 0.6) * 0.6;
    vec3 hemi_ambient = mix(ground_ambient, sky_ambient, hemi);

    // === Compose lighting ===
    vec3 diffuse = albedo.rgb * u_light_color * cel * shadow;
    vec3 ambient = albedo.rgb * hemi_ambient * ao;
    vec3 rim_color = albedo.rgb * rim * u_light_color * 0.8;
    vec3 spec_color = u_light_color * spec * shadow;

    vec3 result = diffuse + ambient + rim_color + spec_color;

    // Minimum brightness floor: ensures the character is always readable.
    // With actual textures loaded (boosted in gbuffer.frag), we can use a
    // slightly lower floor — the texture color variation provides enough
    // visual interest even in shadow. Without textures (flat color), the
    // higher floor prevents the character from looking like a silhouette.
    float min_brightness = 0.40;
    result = max(result, albedo.rgb * min_brightness);

    // === Region hue tinting ===
    result = mix(result, result * u_dominant_hue * 2.5, 0.08);

    // === Corruption color effects ===
    if (u_corruption > 0.25) {
        float intensity = (u_corruption - 0.25) * 1.33;
        float gray = dot(result, vec3(0.299, 0.587, 0.114));
        result = mix(result, vec3(gray * 0.85, gray * 0.65, gray * 0.75), intensity * 0.45);

        if (u_corruption > 0.7) {
            float shift = sin(v_texcoord.y * 50.0 + u_time * 5.0) * (u_corruption - 0.7) * 0.15;
            result.r += shift;
            result.b -= shift;
        }
    }

    // === Distance fog ===
    float dist = length(world_pos - u_camera_pos);
    result = apply_fog(result, dist);

    frag_color = vec4(result, 1.0);
}
