#version 140

uniform mat3 view_normal_matrix;

in vec3 eye_position;

out vec4 out_color;

void main()
{
    vec3 eye_normal = normalize(view_normal_matrix * vec3(0.0, 0.0, 1.0));
    out_color = vec4(eye_normal, -eye_position.z);
}
