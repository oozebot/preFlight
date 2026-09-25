#version 140

in vec3 eye_normal;
in vec3 eye_position;

out vec4 out_color;

void main()
{
    // rgb: eye-space normal; a: positive linear eye depth. Cleared to 0 = background.
    out_color = vec4(normalize(eye_normal), -eye_position.z);
}
