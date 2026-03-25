#version 460 core
// ============================================================================
// G-Buffer Fragment Shader
// Writes position, normal, and albedo to separate render targets.
// Supports both uniform color and texture-mapped albedo.
//
// TRELLIS texture handling: embedded WebP textures are typically very dark
// (avg ~50/255). We apply a brightness boost to bring them into a range
// that works with the cel-shading pipeline while preserving color variation.
// ============================================================================

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_texcoord;

// Material: either uniform color or texture
uniform vec3 u_color;
uniform sampler2D u_diffuse_tex;
uniform float u_has_texture;       // 1.0 = use texture, 0.0 = use u_color
uniform float u_tex_brightness;    // Brightness multiplier for dark TRELLIS textures

layout(location = 0) out vec3 out_position;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec4 out_albedo;

void main() {
    out_position = v_world_pos;
    out_normal = normalize(v_normal);

    if (u_has_texture > 0.5) {
        vec4 tex_color = texture(u_diffuse_tex, v_texcoord);

        // TRELLIS textures are extremely dark (avg brightness ~50/255 ≈ 0.20).
        // Boost them so the cel-shading pipeline has enough color range to work
        // with. The multiplier brings the average into the 0.4-0.6 range where
        // Half-Lambert + cel steps produce visible tonal variation.
        vec3 boosted = tex_color.rgb * u_tex_brightness;

        // Soft clamp to prevent blown-out highlights while preserving variation
        boosted = boosted / (boosted + vec3(0.15));  // Reinhard-lite tone curve

        out_albedo = vec4(boosted, tex_color.a);
    } else {
        out_albedo = vec4(u_color, 1.0);
    }
}
