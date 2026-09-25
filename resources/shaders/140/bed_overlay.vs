#version 140

// Shadow-catcher overlay drawn over the already-rendered bed. Bed triangles are
// in world coordinates, so the world position is the vertex position itself.

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat4 shadow_vp; // world -> shadow-map texture space

in vec3 v_position;

out vec4 shadow_coord;

void main()
{
    shadow_coord = shadow_vp * vec4(v_position, 1.0);
    gl_Position = projection_matrix * view_model_matrix * vec4(v_position, 1.0);
}
