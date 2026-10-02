#version 140

uniform mat3 view_normal_matrix;

in vec3 eye_position;

out vec4 out_color;

void main()
{
    // Same encoding as the gbuffer pass: normal mapped to [0, 1], alpha 1 = geometry.
    vec3 eye_normal = normalize(view_normal_matrix * vec3(0.0, 0.0, 1.0));
    out_color = vec4(eye_normal * 0.5 + 0.5, 1.0);
}
