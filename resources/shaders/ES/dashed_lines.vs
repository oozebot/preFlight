#version 100

// see as reference: https://stackoverflow.com/questions/52928678/dashed-line-in-opengl3

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;

// v_position.w = coordinate along the line
attribute vec4 v_position;

varying float coord_s;

void main()
{
	coord_s = v_position.w;
    gl_Position = projection_matrix * view_model_matrix * vec4(v_position.xyz, 1.0);
}