#version 140

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;

in vec3 v_position;
in vec3 v_normal;

out vec3 eye_normal;
out vec3 eye_position;

void main()
{
    eye_normal = normalize(view_normal_matrix * v_normal);
    vec4 position = view_model_matrix * vec4(v_position, 1.0);
    eye_position = position.xyz;
    gl_Position = projection_matrix * position;
}
