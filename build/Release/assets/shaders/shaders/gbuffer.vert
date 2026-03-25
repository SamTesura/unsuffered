#version 460 core
// ============================================================================
// G-Buffer Geometry Pass — Vertex Shader
// Procedural vertex animation for single-mesh characters (TRELLIS/AI-generated)
//
// DESIGN: Organic motion via overlapping action, eased curves, asymmetry
//   Foundation: vertical bob, lateral rock, forward lean, squash-on-landing
//   Limbs: leg stride with knee bend, arm counterswing, staff pendulum
//   Life: breathing, head bob with drag, idle drift
//   Fluidity: spine twist cascade, weight settling, secondary wobble
//
//   Rules:
//     1. ONLY position offsets (no rotations on continuous mesh)
//     2. All amplitudes proportional to bounding box height (H)
//     3. Smooth center transition via clamp(nx*2.5) — no hard sign()
//     4. Eased curves (sin³, asymmetric waves) — no raw sine robotics
//     5. Overlapping action — body parts peak at staggered times
// ============================================================================

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat3 u_normal_matrix;

uniform float u_walk_phase;
uniform float u_walk_amplitude;
uniform float u_idle_phase;
uniform float u_time;
uniform vec3  u_aabb_min;
uniform vec3  u_aabb_max;
uniform float u_is_animated;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_texcoord;

// ---- Organic waveforms (replace raw sin for living motion) ----

// Eased sine: slow-in slow-out, feels weighted
float esin(float x) {
    float s = sin(x);
    return s * s * s;  // cube preserves sign, creates ease-in/ease-out
}

// Asymmetric wave: fast rise, slow fall — like a foot pushing off then dragging
float asym_wave(float x) {
    float s = sin(x);
    return s > 0.0 ? sqrt(s) : -sqrt(-s);  // sharper peaks, lazier valleys
}

// Snap wave: sharp contact then gradual recovery — footfalls, landing impacts
float snap_wave(float x) {
    float s = sin(x);
    float sharp = sign(s) * pow(abs(s), 0.4);  // sharpened
    return mix(s, sharp, 0.6);  // blend 60% sharp, 40% smooth
}

vec3 AnimateVertex(vec3 pos) {
    vec3 bbox_size = u_aabb_max - u_aabb_min;
    float H = max(bbox_size.y, 0.001);
    float W = max(bbox_size.x, 0.001);
    float D = max(bbox_size.z, 0.001);

    // Normalized coordinates within bounding box
    float ny = clamp((pos.y - u_aabb_min.y) / H, 0.0, 1.0);
    float center_x = (u_aabb_min.x + u_aabb_max.x) * 0.5;
    float center_z = (u_aabb_min.z + u_aabb_max.z) * 0.5;
    float nx = (pos.x - center_x) / (W * 0.5 + 0.001);  // -1..+1
    float nz = (pos.z - center_z) / (D * 0.5 + 0.001);  // -1..+1

    float phase = u_walk_phase;
    float amp   = u_walk_amplitude;
    float idle  = 1.0 - amp;

    // Smooth left/right factor: transitions over middle 40%
    float side = clamp(nx * 2.5, -1.0, 1.0);
    // Distance from centerline (0=center, 1=edges)
    float off_center = smoothstep(0.0, 0.30, abs(nx));
    // Body masks
    float body = smoothstep(0.0, 0.18, ny);
    float upper_body = smoothstep(0.0, 0.60, ny);

    // ================================================================
    // FOUNDATION: Whole-body movement with organic easing
    // ================================================================

    // 1. VERTICAL BOB — double-step with snap-down at footfalls
    //    snap_wave makes the downward dip faster than the recovery (weight!)
    //    Reference shows deep, heavy dips on each footfall — big amplitude
    float bob_raw = snap_wave(phase * 2.0);
    float bob = -abs(bob_raw) * 0.65 + 0.32;
    pos.y += bob * amp * body * H * 0.030;

    // 2. LATERAL ROCK — eased weight transfer (slow at extremes, fast through center)
    //    Reference shows visible side-to-side weight shift — heavier than before
    float rock = esin(phase);
    pos.x += rock * amp * body * H * 0.015;
    // Upper body leans OPPOSITE briefly (overlapping action — torso counters hips)
    pos.x += rock * amp * upper_body * H * 0.004;
    // Delayed upper-body counter-lean (arrives ~15% later than hips)
    float upper_rock = esin(phase - 0.15);
    pos.x -= upper_rock * amp * smoothstep(0.55, 0.75, ny) * H * 0.005;

    // 3. FORWARD LEAN — burdened posture under heavy backpack
    //    Reference character leans forward significantly — the pack is heavy
    pos.z -= amp * upper_body * H * 0.018;
    pos.z += esin(phase * 2.0 + 0.4) * amp * body * H * 0.010;

    // 4. SQUASH ON LANDING — compress at footfalls with overshoot-settle
    float bob_sin = sin(phase * 2.0);
    float squash = max(0.0, -bob_sin);
    squash = squash * squash;
    // Overshoot: brief extra compression then bounce-back
    float settle = max(0.0, -sin(phase * 2.0 + 0.5));
    settle = settle * settle * settle;  // very brief
    float total_squash = squash + settle * 0.3;
    pos.y -= total_squash * amp * ny * H * 0.010;
    pos.x += squash * amp * nx * H * 0.004;

    // ================================================================
    // SPINE TWIST CASCADE — overlapping action up the body
    // Hips lead, then spine, then shoulders, then head — each delayed
    // This is the #1 thing that makes walking look alive vs robotic
    // ================================================================

    float spine_low  = smoothstep(0.20, 0.35, ny) * smoothstep(0.50, 0.35, ny);
    float spine_mid  = smoothstep(0.35, 0.50, ny) * smoothstep(0.65, 0.50, ny);
    float spine_high = smoothstep(0.50, 0.65, ny) * smoothstep(0.78, 0.65, ny);

    // Each level twists (x offset) with increasing phase delay
    float twist_base = sin(phase);
    pos.x += twist_base * amp * spine_low * H * 0.005;                          // hips: on beat
    pos.x += sin(phase - 0.12) * amp * spine_mid * H * 0.004;                   // mid-spine: slight lag
    pos.x += sin(phase - 0.25) * amp * spine_high * H * 0.003;                  // shoulders: more lag
    // Z-axis counter-rotation (shoulders twist opposite to hips)
    pos.z += sin(phase + 0.1) * amp * spine_low * off_center * H * 0.004;
    pos.z -= sin(phase - 0.20) * amp * spine_high * off_center * H * 0.004;

    // ================================================================
    // PER-LIMB: Zone-specific articulation with organic curves
    // ================================================================

    // 5. LEG STRIDE — asymmetric wave for push-off vs swing-through
    float leg_zone = smoothstep(0.38, 0.05, ny);
    float foot_zone = smoothstep(0.12, 0.0, ny);
    float knee_zone = smoothstep(0.25, 0.15, ny) * smoothstep(0.05, 0.15, ny);

    // Slight asymmetry: left leg is 3% faster (creatures aren't perfect)
    float asym = (side > 0.0) ? 1.0 : 0.97;
    float leg_phase = phase * asym + side * 1.5708;

    // Forward/back stride with asymmetric push-off
    // Reference shows very pronounced leg stride — big forward/back reach
    pos.z += asym_wave(leg_phase) * amp * leg_zone * off_center * H * 0.060;

    // Foot lift with sharper peak (foot snaps up then floats down)
    // Reference shows clear foot clearance on each step
    float swing = max(0.0, sin(leg_phase));
    float foot_lift = swing * swing * swing;  // cube = sharper peak, more hang time
    pos.y += foot_lift * amp * foot_zone * off_center * H * 0.028;

    // KNEE BEND — knees push forward during stride (adds depth to leg motion)
    float knee_fwd = max(0.0, sin(leg_phase + 0.8));
    pos.z += knee_fwd * knee_fwd * amp * knee_zone * off_center * H * 0.016;

    // 6. HIP SWAY — hips shift toward planted foot, eased
    float hip_zone = smoothstep(0.20, 0.35, ny) * smoothstep(0.48, 0.35, ny);
    pos.x += esin(phase) * amp * hip_zone * H * 0.010;
    // Hips also tilt slightly (planted side drops)
    pos.y -= abs(esin(phase)) * amp * hip_zone * off_center * side * H * 0.004;

    // 7. ARM SWING — counter to same-side leg with follow-through drag
    float arm_x = smoothstep(0.25, 0.50, abs(nx));
    float arm_y = smoothstep(0.38, 0.52, ny) * smoothstep(0.72, 0.58, ny);
    float arm_blend = arm_x * arm_y;
    float arm_phase = leg_phase + 3.14159;
    // Main swing with easing — reference shows clear arm counter-swing
    pos.z += esin(arm_phase) * amp * arm_blend * H * 0.035;
    // Secondary follow-through: forearm/hand lags behind upper arm
    float forearm_x = smoothstep(0.40, 0.60, abs(nx));  // further out = more hand
    float forearm_lag = esin(arm_phase - 0.35) - esin(arm_phase);  // difference = drag
    pos.z += forearm_lag * amp * forearm_x * arm_y * H * 0.012;
    // Slight inward curl during backswing (arms don't swing in a straight line)
    pos.x -= sin(arm_phase) * amp * arm_blend * side * H * 0.005;

    // 8. STAFF PENDULUM — classic follow-through with secondary wobble
    float staff_dist = smoothstep(0.30, 0.65, abs(nx));
    float staff_low  = smoothstep(0.45, 0.05, ny);
    float staff_blend = staff_dist * staff_low;
    float lag = phase - 0.9;
    // Primary pendulum with easing — reference shows big staff arcs
    pos.z += esin(lag) * amp * staff_blend * H * 0.035;
    pos.x += esin(lag * 0.7) * amp * staff_blend * H * 0.008 * sign(nx);
    // Secondary wobble: staff tip oscillates at 3x frequency (follow-through overshoot)
    float tip_factor = smoothstep(0.25, 0.0, ny);  // stronger at bottom (tip)
    pos.z += sin(lag * 3.0) * amp * staff_blend * tip_factor * H * 0.008;
    pos.x += sin(lag * 2.5 + 0.8) * amp * staff_blend * tip_factor * H * 0.005 * sign(nx);
    // Idle: gentle pendulum drift with dual-frequency organic sway
    pos.x += sin(u_time * 0.6) * idle * staff_blend * H * 0.003;
    pos.z += sin(u_time * 0.4 + 1.5) * idle * staff_blend * H * 0.002;
    pos.x += sin(u_time * 1.1 + 0.3) * idle * staff_blend * tip_factor * H * 0.002;

    // ================================================================
    // LIFE SIGNALS: Organic, always active
    // ================================================================

    // 9. HEAD BOB — delayed from body (overlapping action), with look-drag
    float head_zone = smoothstep(0.78, 0.90, ny);
    // Head bobs LATER than body (receives motion, doesn't generate it)
    pos.y += snap_wave(phase * 2.0 + 0.5) * amp * head_zone * H * 0.008;
    // Head nods forward on each step (delayed from body lean)
    pos.z += esin(phase * 2.0 + 0.3) * amp * head_zone * H * 0.006;
    // Secondary: head has slight lateral wobble from impact (high frequency, low amp)
    pos.x += sin(phase * 2.0 + 1.0) * amp * head_zone * H * 0.004;
    // Idle: organic look-around with two incommensurate frequencies
    pos.x += sin(u_time * 0.25) * idle * head_zone * H * 0.003;
    pos.z += sin(u_time * 0.18 + 1.2) * idle * head_zone * H * 0.002;
    // Slow head tilt (feels contemplative — living creature)
    pos.y += sin(u_time * 0.12 + 2.5) * idle * head_zone * H * 0.001;

    // 10. BREATHING — chest expansion + shoulder rise (always active, blends with walk)
    float chest = smoothstep(0.40, 0.55, ny) * smoothstep(0.72, 0.55, ny);
    float breathe = u_time * mix(1.0, 1.8, amp);  // faster when walking
    // Asymmetric breathing: inhale slower than exhale
    float breath_wave = sin(breathe) * 0.6 + sin(breathe * 2.0) * 0.15;
    pos.z -= breath_wave * chest * H * 0.003;
    float shoulders = smoothstep(0.58, 0.68, ny) * smoothstep(0.76, 0.68, ny);
    pos.y += breath_wave * shoulders * H * 0.002;
    // Belly expansion (counterpoint to chest — inhale pushes belly out)
    float belly = smoothstep(0.28, 0.38, ny) * smoothstep(0.48, 0.38, ny);
    pos.z += sin(breathe + 0.3) * belly * H * 0.002;

    // 11. IDLE DRIFT — weary swaying with two incommensurate frequencies
    //     (non-repeating pattern feels more organic than single sine)
    pos.x += sin(u_time * 0.22) * idle * body * H * 0.004;
    pos.x += sin(u_time * 0.37 + 1.7) * idle * body * H * 0.002;  // second sway layer
    pos.z += sin(u_time * 0.15 + 0.8) * idle * body * H * 0.002;
    pos.y += sin(u_idle_phase) * idle * chest * H * 0.003;
    // Slight weight shift (center of mass wanders)
    pos.z += sin(u_time * 0.09 + 3.1) * idle * upper_body * H * 0.002;

    return pos;
}

void main() {
    vec3 animated_pos = a_position;
    vec3 animated_normal = a_normal;

    if (u_is_animated > 0.5) {
        animated_pos = AnimateVertex(a_position);

        // Approximate deformed normal via finite-difference Jacobian
        float eps = max(u_aabb_max.y - u_aabb_min.y, 0.001) * 0.001;
        vec3 px = AnimateVertex(a_position + vec3(eps, 0, 0));
        vec3 mx = AnimateVertex(a_position - vec3(eps, 0, 0));
        vec3 py = AnimateVertex(a_position + vec3(0, eps, 0));
        vec3 my = AnimateVertex(a_position - vec3(0, eps, 0));
        vec3 pz = AnimateVertex(a_position + vec3(0, 0, eps));
        vec3 mz = AnimateVertex(a_position - vec3(0, 0, eps));
        mat3 J = mat3(px - mx, py - my, pz - mz) / (2.0 * eps);
        animated_normal = normalize(transpose(inverse(J)) * a_normal);
    }

    vec4 world_pos = u_model * vec4(animated_pos, 1.0);
    v_world_pos = world_pos.xyz;
    v_normal = normalize(u_normal_matrix * animated_normal);
    v_texcoord = a_texcoord;
    gl_Position = u_projection * u_view * world_pos;
}
