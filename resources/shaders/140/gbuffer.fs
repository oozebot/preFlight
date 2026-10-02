#version 140

in vec3 eye_normal;
in vec3 eye_position;

out vec4 out_color;

void main()
{
    // rgb: eye-space normal mapped to [0, 1]; a: 1 = geometry, cleared to 0 = background.
    // Eye positions are rebuilt from the depth attachment.
    out_color = vec4(normalize(eye_normal) * 0.5 + 0.5, 1.0);
}
