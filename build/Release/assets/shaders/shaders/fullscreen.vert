#version 460 core
// Fullscreen quad for post-processing passes (cel, outline, corruption, UI)

out vec2 v_texcoord;

void main() {
    // Generate fullscreen triangle (no vertex buffer needed)
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    v_texcoord = positions[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
