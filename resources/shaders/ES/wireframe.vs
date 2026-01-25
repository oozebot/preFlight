#version 100

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform float offset;

attribute vec3 v_position;
attribute vec3 v_normal;
attribute vec3 v_extra;

varying vec3 barycentric;

void main()
{
	barycentric = v_extra;
    // Add small epsilon to z to solve z-fighting
	vec4 clip_position = projection_matrix * view_model_matrix * vec4(v_position, 1.0);
	clip_position.z -= offset * abs(clip_position.w);
    gl_Position = clip_position;
}