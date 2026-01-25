#version 100

precision highp float;

uniform sampler2D uniform_texture;

varying vec2 tex_coord;

void main()
{
    gl_FragColor = texture2D(uniform_texture, tex_coord);
}
