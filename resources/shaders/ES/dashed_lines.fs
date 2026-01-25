#version 100

precision highp float;

// see as reference: https://stackoverflow.com/questions/52928678/dashed-line-in-opengl3

uniform float dash_size;
uniform float gap_size;
uniform vec4 uniform_color;

varying float coord_s;

void main()
{
	float inv_stride = 1.0 / (dash_size + gap_size);
    if (gap_size > 0.0 && fract(coord_s * inv_stride) > dash_size * inv_stride)
        discard;
		
	gl_FragColor = uniform_color;
}
