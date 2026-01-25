#version 100
#extension GL_OES_standard_derivatives : enable

// see for reference: https://stackoverflow.com/questions/7361582/opengl-debugging-single-pass-wireframe-rendering

precision highp float;

uniform vec4 uniform_color;

varying vec3 barycentric;

void main()
{
    float min_dist = min(min(barycentric.x, barycentric.y), barycentric.z);
	if (min_dist > 0.5 * fwidth(min_dist))
		discard;
	
    gl_FragColor = uniform_color;
}