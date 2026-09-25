#version 140

const vec3 ZERO = vec3(0.0, 0.0, 0.0);

struct SlopeDetection
{
    bool actived;
    float normal_z;
    mat3 volume_world_normal_matrix;
    vec3 color;
};

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;
uniform mat4 volume_world_matrix;
uniform mat4 shadow_vp; // world -> shadow-map texture space
uniform SlopeDetection slope;

uniform vec2 z_range;
uniform vec4 clipping_plane;
uniform vec4 color_clip_plane;

in vec3 v_position;
in vec3 v_normal;

out vec3 eye_normal;
out vec3 eye_position;
out vec3 clipping_planes_dots;
out float color_clip_plane_dot;
out vec4 world_pos;
out float world_normal_z;
out vec4 shadow_coord;

void main()
{
    eye_normal = normalize(view_normal_matrix * v_normal);

    vec4 position = view_model_matrix * vec4(v_position, 1.0);
    eye_position = position.xyz;

    world_pos = volume_world_matrix * vec4(v_position, 1.0);
    shadow_coord = shadow_vp * vec4(world_pos.xyz, 1.0);

    world_normal_z = slope.actived ? (normalize(slope.volume_world_normal_matrix * v_normal)).z : 0.0;

    gl_Position = projection_matrix * position;
    clipping_planes_dots = vec3(dot(world_pos, clipping_plane), world_pos.z - z_range.x, z_range.y - world_pos.z);
    color_clip_plane_dot = dot(world_pos, color_clip_plane);
}
