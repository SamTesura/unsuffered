#version 460 core
// ============================================================================
// G-Buffer Geometry Pass — Vertex Shader
// Procedural vertex animation for single-mesh characters (TRELLIS/AI-generated)
//
// DESIGN: Burdened, exhausted warrior trudge — Grak is weary, armored, heavy
//   Foundation: deep sinking bob, wide weary rock, heavy forward hunch
//   Limbs: dragging leg stride, deadweight arm pendulum, shoulder roll
//   Life: labored breathing, low head drag, exhausted idle drift
//   Fluidity: spine twist cascade, gear mass sway, asymmetric limp
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
    // FOUNDATION: Burdened warrior — every step costs energy
    // ================================================================

    // 1. VERTICAL BOB — deep sinking on each footfall, slow heavy recovery
    //    Reference: the whole body drops hard on contact, drags itself back up
    float bob_raw = snap_wave(phase * 2.0);
    float bob = -abs(bob_raw) * 0.80 + 0.38;  // deeper dip, offset so feet don't clip
    pos.y += bob * amp * body * H * 0.042;
    // Extra: upper body sinks MORE than lower (top-heavy armor compresses)
    pos.y -= abs(bob_raw) * amp * upper_body * H * 0.006;

    // 2. LATERAL ROCK — wide, weary weight transfer
    //    Reference: big side-to-side lean, almost dragging each step across
    float rock = esin(phase);
    pos.x += rock * amp * body * H * 0.022;
    // Upper body counter-lean with heavy delay (torso fights momentum)
    float upper_rock = esin(phase - 0.25);  // more delay = more drag
    pos.x -= upper_rock * amp * smoothstep(0.50, 0.72, ny) * H * 0.008;
    // Hips shift into the planted foot (weight settling)
    pos.x += esin(phase + 0.3) * amp * smoothstep(0.15, 0.30, ny) * smoothstep(0.42, 0.30, ny) * H * 0.006;

    // 3. FORWARD HUNCH — burdened posture, shoulders rolled forward, head down
    //    Reference: extreme forward lean, carrying the weight of the world
    //    Static hunch (always active when walking):
    pos.z -= amp * upper_body * H * 0.028;
    //    Rhythmic forward surge on each step (lurch):
    pos.z += esin(phase * 2.0 + 0.4) * amp * body * H * 0.016;
    //    Upper back rounds forward more than lower (hunched shoulders):
    float upper_back = smoothstep(0.55, 0.75, ny);
    pos.z -= amp * upper_back * H * 0.010;

    // 4. SQUASH ON LANDING — heavy compression with slow settle
    float bob_sin = sin(phase * 2.0);
    float squash = max(0.0, -bob_sin);
    squash = squash * squash;
    // Overshoot: extra compression then sluggish bounce-back
    float settle = max(0.0, -sin(phase * 2.0 + 0.5));
    settle = settle * settle * settle;
    float total_squash = squash + settle * 0.4;  // more overshoot than before
    pos.y -= total_squash * amp * ny * H * 0.014;
    pos.x += squash * amp * nx * H * 0.005;

    // ================================================================
    // SPINE TWIST CASCADE — exhausted overlapping action
    // Wider delays between segments = more drag = more weary
    // ================================================================

    float spine_low  = smoothstep(0.20, 0.35, ny) * smoothstep(0.50, 0.35, ny);
    float spine_mid  = smoothstep(0.35, 0.50, ny) * smoothstep(0.65, 0.50, ny);
    float spine_high = smoothstep(0.50, 0.65, ny) * smoothstep(0.78, 0.65, ny);

    // Each level twists with WIDER phase delay (weary = more lag between body parts)
    float twist_base = sin(phase);
    pos.x += twist_base * amp * spine_low * H * 0.007;                          // hips: on beat
    pos.x += sin(phase - 0.18) * amp * spine_mid * H * 0.006;                   // mid-spine: lagging
    pos.x += sin(phase - 0.38) * amp * spine_high * H * 0.005;                  // shoulders: heavy lag
    // Z-axis counter-rotation (broader, more visible)
    pos.z += sin(phase + 0.1) * amp * spine_low * off_center * H * 0.006;
    pos.z -= sin(phase - 0.30) * amp * spine_high * off_center * H * 0.006;

    // ================================================================
    // PER-LIMB: Heavy, deliberate, dragging
    // ================================================================

    // 5. LEG STRIDE — wide trudging steps, heavy foot placement
    //    Reference: slow deliberate stride, foot almost drags before lifting
    float leg_zone = smoothstep(0.40, 0.05, ny);  // slightly higher blend = thigh involvement
    float foot_zone = smoothstep(0.14, 0.0, ny);
    float knee_zone = smoothstep(0.28, 0.16, ny) * smoothstep(0.05, 0.16, ny);

    // Stronger asymmetry: favored side (slight limp, one leg 6% slower)
    float asym = (side > 0.0) ? 1.0 : 0.94;
    float leg_phase = phase * asym + side * 1.5708;

    // Forward/back stride — wide, heavy, deliberate
    pos.z += asym_wave(leg_phase) * amp * leg_zone * off_center * H * 0.055;

    // Foot lift: lower clearance, more drag-feel (squared instead of cubed = lazier arc)
    float swing_val = max(0.0, sin(leg_phase));
    float foot_lift = swing_val * swing_val;  // squared = lazier lift, less snap
    pos.y += foot_lift * amp * foot_zone * off_center * H * 0.022;

    // Foot DRAG: on the down-phase, foot pushes back slightly (scuffing the ground)
    float drag_phase = max(0.0, -sin(leg_phase + 0.3));
    pos.z -= drag_phase * drag_phase * amp * foot_zone * off_center * H * 0.010;

    // KNEE BEND — knees push forward heavier during stride
    float knee_fwd = max(0.0, sin(leg_phase + 0.8));
    pos.z += knee_fwd * knee_fwd * amp * knee_zone * off_center * H * 0.020;

    // Asymmetric hip drop: favored side dips more (limp accent)
    float limp_extra = (side < 0.0) ? 0.4 : 0.0;  // left side dips more
    pos.y -= limp_extra * squash * amp * leg_zone * off_center * H * 0.008;

    // 6. HIP SWAY — heavy hips shifting toward planted foot
    float hip_zone = smoothstep(0.20, 0.35, ny) * smoothstep(0.48, 0.35, ny);
    pos.x += esin(phase) * amp * hip_zone * H * 0.014;
    // Hips tilt: planted side drops under weight
    pos.y -= abs(esin(phase)) * amp * hip_zone * off_center * side * H * 0.006;

    // 7. ARM SWING — deadweight pendulums, hanging heavy
    //    Reference: arms hang loose, swing like weighted ropes
    float arm_x = smoothstep(0.22, 0.48, abs(nx));
    float arm_y = smoothstep(0.35, 0.50, ny) * smoothstep(0.72, 0.55, ny);
    float arm_blend = arm_x * arm_y;
    float arm_phase = leg_phase + 3.14159;
    // Main swing: heavy pendulum, eased — more amplitude, slower feel
    pos.z += esin(arm_phase) * amp * arm_blend * H * 0.045;
    // Secondary follow-through: forearm/hand lags MUCH more (heavy drag)
    float forearm_x = smoothstep(0.38, 0.62, abs(nx));
    float forearm_lag = esin(arm_phase - 0.50) - esin(arm_phase);  // bigger lag = more drag
    pos.z += forearm_lag * amp * forearm_x * arm_y * H * 0.018;
    // Arms hang slightly outward (don't press against body)
    pos.x += abs(sin(arm_phase)) * amp * arm_blend * sign(nx) * H * 0.004;
    // Slight inward curl during backswing
    pos.x -= sin(arm_phase) * amp * arm_blend * side * H * 0.006;

    // 8. SHOULDER ROLL — heavy shoulders rolling with each stride
    //    Reference: visible shoulder drop/rise alternation, carrying weight
    float shoulder_zone = smoothstep(0.58, 0.72, ny) * smoothstep(0.82, 0.72, ny);
    float shoulder_outer = smoothstep(0.28, 0.52, abs(nx));
    float shoulder_blend = shoulder_zone * shoulder_outer;
    // Leading shoulder drops and pushes forward (carrying burden)
    float shoulder_phase = phase + side * 1.5708;
    pos.y -= esin(shoulder_phase) * amp * shoulder_blend * H * 0.012;
    pos.z += esin(shoulder_phase - 0.25) * amp * shoulder_blend * H * 0.014;
    // Shoulders hunch inward under load (protective posture)
    pos.x -= amp * shoulder_blend * sign(nx) * H * 0.005;

    // GEAR MASS SWAY — secondary motion from heavy pack/armor on upper back
    //    Reference: visible mass on back sways with delay behind body
    float gear_zone = smoothstep(0.48, 0.62, ny) * smoothstep(0.78, 0.62, ny);
    float gear_back = smoothstep(-0.2, -0.6, nz);  // back-facing vertices
    float gear_blend = gear_zone * max(gear_back, 0.3);  // some everywhere, more on back
    float gear_lag = phase - 0.6;  // delayed behind body
    pos.z += esin(gear_lag) * amp * gear_blend * H * 0.008;
    pos.x += esin(gear_lag * 0.7) * amp * gear_blend * H * 0.005;
    // Idle: gear settles and drifts slowly
    pos.z += sin(u_time * 0.3 + 1.5) * idle * gear_blend * H * 0.002;

    // Hand/fist clench — lower arm extremities sway heavy
    float hand_zone = smoothstep(0.55, 0.40, ny) * smoothstep(0.28, 0.40, ny);
    float hand_outer = smoothstep(0.45, 0.72, abs(nx));
    float hand_blend = hand_zone * hand_outer;
    // Hands swing heavy, curl inward slightly (fist rhythm)
    pos.x -= esin(arm_phase + 0.3) * amp * hand_blend * side * H * 0.010;
    pos.y -= max(0.0, sin(arm_phase)) * amp * hand_blend * H * 0.007;
    // Idle: loose hand dangle with dual-frequency drift
    pos.x += sin(u_time * 0.4 + side * 1.2) * idle * hand_blend * H * 0.004;
    pos.z += sin(u_time * 0.28 + 1.8) * idle * hand_blend * H * 0.003;

    // ================================================================
    // LIFE SIGNALS: Exhausted but alive
    // ================================================================

    // 9. HEAD — stays low, barely bobs, nods forward on footfalls
    //    Reference: head is down, moves WITH the body, doesn't lift much
    float head_zone = smoothstep(0.78, 0.92, ny);
    // Minimal bob — head mostly just follows body, slight extra dip
    pos.y += snap_wave(phase * 2.0 + 0.5) * amp * head_zone * H * 0.005;
    // Head nods forward with each step (weary nod, heavier than before)
    pos.z += esin(phase * 2.0 + 0.3) * amp * head_zone * H * 0.010;
    // Head stays forward/down (hunched posture, constant)
    pos.z -= amp * head_zone * H * 0.008;
    // Minimal lateral — just residual from body impact
    pos.x += sin(phase * 2.0 + 1.0) * amp * head_zone * H * 0.003;
    // Idle: slow, weary look-around
    pos.x += sin(u_time * 0.18) * idle * head_zone * H * 0.004;
    pos.z += sin(u_time * 0.12 + 1.2) * idle * head_zone * H * 0.003;
    // Idle head droop (fatigue — head slowly drops and recovers)
    pos.y -= sin(u_time * 0.08 + 2.5) * idle * head_zone * H * 0.002;

    // 10. BREATHING — labored, heavier when walking
    float chest = smoothstep(0.40, 0.55, ny) * smoothstep(0.72, 0.55, ny);
    float breathe = u_time * mix(0.8, 2.0, amp);  // labored: slower at rest, faster when moving
    // Asymmetric breathing: inhale slower than exhale (labored)
    float breath_wave = sin(breathe) * 0.7 + sin(breathe * 2.0) * 0.18;
    pos.z -= breath_wave * chest * H * 0.004;
    float shoulders_br = smoothstep(0.58, 0.68, ny) * smoothstep(0.76, 0.68, ny);
    pos.y += breath_wave * shoulders_br * H * 0.003;
    // Belly heaves (exhaustion — bigger belly motion)
    float belly = smoothstep(0.26, 0.38, ny) * smoothstep(0.50, 0.38, ny);
    pos.z += sin(breathe + 0.3) * belly * H * 0.004;

    // 11. IDLE DRIFT — exhausted swaying, barely standing
    //     Bigger, slower drift than an alert creature — on the edge of collapse
    pos.x += sin(u_time * 0.15) * idle * body * H * 0.006;
    pos.x += sin(u_time * 0.28 + 1.7) * idle * body * H * 0.003;
    pos.z += sin(u_time * 0.10 + 0.8) * idle * body * H * 0.004;
    pos.y += sin(u_idle_phase) * idle * chest * H * 0.004;
    // Weight wanders: center of mass drifts (about to sway over)
    pos.z += sin(u_time * 0.07 + 3.1) * idle * upper_body * H * 0.003;
    // Idle forward droop: body slowly leans forward then catches itself
    pos.z -= sin(u_time * 0.05) * idle * upper_body * H * 0.003;

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
