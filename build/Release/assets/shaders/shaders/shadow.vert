#version 460 core
// Shadow Pass — MUST match gbuffer.vert AnimateVertex exactly

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_light_space;
uniform mat4 u_model;
uniform float u_walk_phase;
uniform float u_walk_amplitude;
uniform float u_idle_phase;
uniform float u_time;
uniform vec3  u_aabb_min;
uniform vec3  u_aabb_max;
uniform float u_is_animated;

// ---- Organic waveforms (must match gbuffer.vert exactly) ----

float esin(float x) {
    float s = sin(x);
    return s * s * s;
}

float asym_wave(float x) {
    float s = sin(x);
    return s > 0.0 ? sqrt(s) : -sqrt(-s);
}

float snap_wave(float x) {
    float s = sin(x);
    float sharp = sign(s) * pow(abs(s), 0.4);
    return mix(s, sharp, 0.6);
}

vec3 AnimateVertex(vec3 pos) {
    vec3 bbox_size = u_aabb_max - u_aabb_min;
    float H = max(bbox_size.y, 0.001);
    float W = max(bbox_size.x, 0.001);
    float D = max(bbox_size.z, 0.001);

    float ny = clamp((pos.y - u_aabb_min.y) / H, 0.0, 1.0);
    float center_x = (u_aabb_min.x + u_aabb_max.x) * 0.5;
    float center_z = (u_aabb_min.z + u_aabb_max.z) * 0.5;
    float nx = (pos.x - center_x) / (W * 0.5 + 0.001);
    float nz = (pos.z - center_z) / (D * 0.5 + 0.001);

    float phase = u_walk_phase;
    float amp   = u_walk_amplitude;
    float idle  = 1.0 - amp;

    float side = clamp(nx * 2.5, -1.0, 1.0);
    float off_center = smoothstep(0.0, 0.30, abs(nx));
    float body = smoothstep(0.0, 0.18, ny);
    float upper_body = smoothstep(0.0, 0.60, ny);

    // 1. Vertical bob
    float bob_raw = snap_wave(phase * 2.0);
    float bob = -abs(bob_raw) * 0.65 + 0.32;
    pos.y += bob * amp * body * H * 0.030;

    // 2. Lateral rock
    float rock = esin(phase);
    pos.x += rock * amp * body * H * 0.015;
    pos.x += rock * amp * upper_body * H * 0.004;
    float upper_rock = esin(phase - 0.15);
    pos.x -= upper_rock * amp * smoothstep(0.55, 0.75, ny) * H * 0.005;

    // 3. Forward lean
    pos.z -= amp * upper_body * H * 0.018;
    pos.z += esin(phase * 2.0 + 0.4) * amp * body * H * 0.010;

    // 4. Squash on landing
    float bob_sin = sin(phase * 2.0);
    float squash = max(0.0, -bob_sin);
    squash = squash * squash;
    float settle = max(0.0, -sin(phase * 2.0 + 0.5));
    settle = settle * settle * settle;
    float total_squash = squash + settle * 0.3;
    pos.y -= total_squash * amp * ny * H * 0.010;
    pos.x += squash * amp * nx * H * 0.004;

    // Spine twist cascade
    float spine_low  = smoothstep(0.20, 0.35, ny) * smoothstep(0.50, 0.35, ny);
    float spine_mid  = smoothstep(0.35, 0.50, ny) * smoothstep(0.65, 0.50, ny);
    float spine_high = smoothstep(0.50, 0.65, ny) * smoothstep(0.78, 0.65, ny);
    float twist_base = sin(phase);
    pos.x += twist_base * amp * spine_low * H * 0.005;
    pos.x += sin(phase - 0.12) * amp * spine_mid * H * 0.004;
    pos.x += sin(phase - 0.25) * amp * spine_high * H * 0.003;
    pos.z += sin(phase + 0.1) * amp * spine_low * off_center * H * 0.004;
    pos.z -= sin(phase - 0.20) * amp * spine_high * off_center * H * 0.004;

    // 5. Leg stride
    float leg_zone = smoothstep(0.38, 0.05, ny);
    float foot_zone = smoothstep(0.12, 0.0, ny);
    float knee_zone = smoothstep(0.25, 0.15, ny) * smoothstep(0.05, 0.15, ny);
    float asym = (side > 0.0) ? 1.0 : 0.97;
    float leg_phase = phase * asym + side * 1.5708;
    pos.z += asym_wave(leg_phase) * amp * leg_zone * off_center * H * 0.060;
    float swing = max(0.0, sin(leg_phase));
    float foot_lift = swing * swing * swing;
    pos.y += foot_lift * amp * foot_zone * off_center * H * 0.028;
    float knee_fwd = max(0.0, sin(leg_phase + 0.8));
    pos.z += knee_fwd * knee_fwd * amp * knee_zone * off_center * H * 0.016;

    // 6. Hip sway
    float hip_zone = smoothstep(0.20, 0.35, ny) * smoothstep(0.48, 0.35, ny);
    pos.x += esin(phase) * amp * hip_zone * H * 0.010;
    pos.y -= abs(esin(phase)) * amp * hip_zone * off_center * side * H * 0.004;

    // 7. Arm swing
    float arm_x = smoothstep(0.25, 0.50, abs(nx));
    float arm_y = smoothstep(0.38, 0.52, ny) * smoothstep(0.72, 0.58, ny);
    float arm_blend = arm_x * arm_y;
    float arm_phase = leg_phase + 3.14159;
    pos.z += esin(arm_phase) * amp * arm_blend * H * 0.035;
    float forearm_x = smoothstep(0.40, 0.60, abs(nx));
    float forearm_lag = esin(arm_phase - 0.35) - esin(arm_phase);
    pos.z += forearm_lag * amp * forearm_x * arm_y * H * 0.012;
    pos.x -= sin(arm_phase) * amp * arm_blend * side * H * 0.005;

    // 8. Staff pendulum
    float staff_dist = smoothstep(0.30, 0.65, abs(nx));
    float staff_low  = smoothstep(0.45, 0.05, ny);
    float staff_blend = staff_dist * staff_low;
    float lag = phase - 0.9;
    pos.z += esin(lag) * amp * staff_blend * H * 0.035;
    pos.x += esin(lag * 0.7) * amp * staff_blend * H * 0.008 * sign(nx);
    float tip_factor = smoothstep(0.25, 0.0, ny);
    pos.z += sin(lag * 3.0) * amp * staff_blend * tip_factor * H * 0.008;
    pos.x += sin(lag * 2.5 + 0.8) * amp * staff_blend * tip_factor * H * 0.005 * sign(nx);
    pos.x += sin(u_time * 0.6) * idle * staff_blend * H * 0.003;
    pos.z += sin(u_time * 0.4 + 1.5) * idle * staff_blend * H * 0.002;
    pos.x += sin(u_time * 1.1 + 0.3) * idle * staff_blend * tip_factor * H * 0.002;

    // 9. Head bob
    float head_zone = smoothstep(0.78, 0.90, ny);
    pos.y += snap_wave(phase * 2.0 + 0.5) * amp * head_zone * H * 0.008;
    pos.z += esin(phase * 2.0 + 0.3) * amp * head_zone * H * 0.006;
    pos.x += sin(phase * 2.0 + 1.0) * amp * head_zone * H * 0.004;
    pos.x += sin(u_time * 0.25) * idle * head_zone * H * 0.003;
    pos.z += sin(u_time * 0.18 + 1.2) * idle * head_zone * H * 0.002;
    pos.y += sin(u_time * 0.12 + 2.5) * idle * head_zone * H * 0.001;

    // 10. Breathing
    float chest = smoothstep(0.40, 0.55, ny) * smoothstep(0.72, 0.55, ny);
    float breathe = u_time * mix(1.0, 1.8, amp);
    float breath_wave = sin(breathe) * 0.6 + sin(breathe * 2.0) * 0.15;
    pos.z -= breath_wave * chest * H * 0.003;
    float shoulders = smoothstep(0.58, 0.68, ny) * smoothstep(0.76, 0.68, ny);
    pos.y += breath_wave * shoulders * H * 0.002;
    float belly = smoothstep(0.28, 0.38, ny) * smoothstep(0.48, 0.38, ny);
    pos.z += sin(breathe + 0.3) * belly * H * 0.002;

    // 11. Idle drift
    pos.x += sin(u_time * 0.22) * idle * body * H * 0.004;
    pos.x += sin(u_time * 0.37 + 1.7) * idle * body * H * 0.002;
    pos.z += sin(u_time * 0.15 + 0.8) * idle * body * H * 0.002;
    pos.y += sin(u_idle_phase) * idle * chest * H * 0.003;
    pos.z += sin(u_time * 0.09 + 3.1) * idle * upper_body * H * 0.002;

    return pos;
}

void main() {
    vec3 pos = a_position;
    if (u_is_animated > 0.5) {
        pos = AnimateVertex(a_position);
    }
    gl_Position = u_light_space * u_model * vec4(pos, 1.0);
}
