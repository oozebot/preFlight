#version 140

// G-buffer pass for the bed surface: flat geometry with no vertex normals,
// so the fragment stage uses a constant up normal.

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;

in vec3 v_position;

out vec3 eye_position;

void main()
{
    vec4 position = view_model_matrix * vec4(v_position, 1.0);
    eye_position = position.xyz;
    gl_Position = projection_matrix * position;
}
