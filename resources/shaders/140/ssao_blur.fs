#version 140

// Separable 5-tap blur for the SSAO texture; run once horizontally, once vertically.

uniform sampler2D ao_tex;
uniform vec2 blur_dir; // one texel step in the blur direction

in vec2 tex_coord;

out vec4 out_color;

void main()
{
    float sum = 0.0;
    sum += texture(ao_tex, tex_coord - 2.0 * blur_dir).r * 0.13;
    sum += texture(ao_tex, tex_coord - blur_dir).r * 0.23;
    sum += texture(ao_tex, tex_coord).r * 0.28;
    sum += texture(ao_tex, tex_coord + blur_dir).r * 0.23;
    sum += texture(ao_tex, tex_coord + 2.0 * blur_dir).r * 0.13;
    out_color = vec4(vec3(sum), 1.0);
}
