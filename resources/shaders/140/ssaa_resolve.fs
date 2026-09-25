#version 140

// Downsamples the supersampled scene to the native framebuffer, carrying depth
// along so later passes that read or test against the depth buffer keep working.

uniform sampler2D color_tex;
uniform sampler2D depth_tex;

in vec2 tex_coord;

out vec4 out_color;

void main()
{
    out_color = texture(color_tex, tex_coord);
    gl_FragDepth = texture(depth_tex, tex_coord).r;
}
