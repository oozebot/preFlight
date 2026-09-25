///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

// needed for tech VGCODE_ENABLE_COG_AND_TOOL_MARKERS
#include "../include/Types.hpp"

// RPi 5 V3D GPU supports GLSL 1.40 max (OpenGL 3.1); other platforms use GLSL 1.50 (OpenGL 3.2)
#if defined(__linux__) && defined(__aarch64__)
#define VGCODE_GLSL_VERSION "#version 140\n"
#else
#define VGCODE_GLSL_VERSION "#version 150\n"
#endif

namespace libvgcode
{

// Declarations and imposter-geometry body shared by the visible segments shader and
// the scene-pass G-buffer variant. The body leaves main() open; each variant appends
// its outputs and the closing brace.
#define VGCODE_SEGMENTS_VS_DECLS                                                                                                                            \
    "#define POINTY_CAPS\n"                                                                                                                                 \
    "#define FIX_TWISTING\n"                                                                                                                                \
    "const vec3  light_top_dir = vec3(-0.4574957, 0.4574957, 0.7624929);\n"                                                                                 \
    "const float light_top_diffuse = 0.6 * 0.8;\n"                                                                                                          \
    "const float light_top_specular = 0.6 * 0.125;\n"                                                                                                       \
    "const float light_top_shininess = 20.0;\n"                                                                                                             \
    "const vec3  light_front_dir = vec3(0.6985074, 0.1397015, 0.6985074);\n"                                                                                \
    "const float light_front_diffuse = 0.6 * 0.3;\n"                                                                                                        \
    "const float ambient = 0.3;\n"                                                                                                                          \
    "const float emission = 0.15;\n"                                                                                                                        \
    "const vec3 UP = vec3(0, 0, 1);\n"                                                                                                                      \
    "uniform mat4 view_matrix;\n"                                                                                                                           \
    "uniform mat4 projection_matrix;\n"                                                                                                                     \
    "uniform vec3 camera_position;\n"                                                                                                                       \
    "uniform vec4 clipping_plane;\n"                                                                                                                        \
    "uniform samplerBuffer position_tex;\n"                                                                                                                 \
    "uniform samplerBuffer height_width_angle_tex;\n"                                                                                                       \
    "uniform samplerBuffer color_tex;\n"                                                                                                                    \
    "uniform usamplerBuffer segment_index_tex;\n"                                                                                                           \
    "in int vertex_id;\n"                                                                                                                                   \
    "out float clipping_dist;\n"                                                                                                                            \
    "vec3 decode_color(float color) {\n"                                                                                                                    \
    "  int c = int(round(color));\n"                                                                                                                        \
    "  int r = (c >> 16) & 0xFF;\n"                                                                                                                         \
    "  int g = (c >> 8) & 0xFF;\n"                                                                                                                          \
    "  int b = (c >> 0) & 0xFF;\n"                                                                                                                          \
    "  float f = 1.0 / 255.0f;\n"                                                                                                                           \
    "  return f * vec3(r, g, b);\n"                                                                                                                         \
    "}\n"                                                                                                                                                   \
    "float lighting(vec3 eye_position, vec3 eye_normal) {\n"                                                                                                \
    "  float top_diffuse = light_top_diffuse * max(dot(eye_normal, light_top_dir), 0.0);\n"                                                                 \
    "  float front_diffuse = light_front_diffuse * max(dot(eye_normal, light_front_dir), 0.0);\n"                                                           \
    "  float top_specular = light_top_specular * pow(max(dot(-normalize(eye_position), reflect(-light_top_dir, eye_normal)), 0.0), light_top_shininess);\n" \
    "  return ambient + top_diffuse + front_diffuse + top_specular + emission;\n"                                                                           \
    "}\n"

#define VGCODE_SEGMENTS_VS_BODY                                                                                                                          \
    "void main() {\n"                                                                                                                                    \
    "  int id_a = int(texelFetch(segment_index_tex, gl_InstanceID).r);\n"                                                                                \
    "  int id_b = id_a + 1;\n"                                                                                                                           \
    "  vec3 pos_a = texelFetch(position_tex, id_a).xyz;\n"                                                                                               \
    "  vec3 pos_b = texelFetch(position_tex, id_b).xyz;\n"                                                                                               \
    "  // Cap vertex mapping for clip plane cross-section caps\n"                                                                                        \
    "  bool is_cap = (vertex_id >= 8);\n"                                                                                                                \
    "  int eff_id = vertex_id;\n"                                                                                                                        \
    "  if (is_cap) { const int cm[8] = int[](0,1,3,5,6,4,0,5); eff_id = cm[vertex_id-8]; }\n"                                                            \
    "  vec3 line = pos_b - pos_a;\n"                                                                                                                     \
    "  // directions of the line box in world space\n"                                                                                                   \
    "  float line_len = length(line);\n"                                                                                                                 \
    "  vec3 line_dir;\n"                                                                                                                                 \
    "  if (line_len < 1e-4)\n"                                                                                                                           \
    "    line_dir = vec3(1.0, 0.0, 0.0);\n"                                                                                                              \
    "  else\n"                                                                                                                                           \
    "    line_dir = line / line_len;\n"                                                                                                                  \
    "  vec3 line_right_dir;\n"                                                                                                                           \
    "  if (abs(dot(line_dir, UP)) > 0.9) {\n"                                                                                                            \
    "    // For vertical lines, the width and height should be same, there is no concept of up and down.\n"                                              \
    "    // For simplicity, the code will expand width in the x axis, and height in the y axis\n"                                                        \
    "    line_right_dir = normalize(cross(vec3(1, 0, 0), line_dir));\n"                                                                                  \
    "  }\n"                                                                                                                                              \
    "  else\n"                                                                                                                                           \
    "    line_right_dir = normalize(cross(line_dir, UP));\n"                                                                                             \
    "  vec3 line_up_dir = normalize(cross(line_right_dir, line_dir));\n"                                                                                 \
    "  const vec2 horizontal_vertical_view_signs_array[16] = vec2[](\n"                                                                                  \
    "    //horizontal view (from right)\n"                                                                                                               \
    "    vec2(1.0, 0.0),\n"                                                                                                                              \
    "    vec2(0.0, 1.0),\n"                                                                                                                              \
    "    vec2(0.0, 0.0),\n"                                                                                                                              \
    "    vec2(0.0, -1.0),\n"                                                                                                                             \
    "    vec2(0.0, -1.0),\n"                                                                                                                             \
    "    vec2(1.0, 0.0),\n"                                                                                                                              \
    "    vec2(0.0, 1.0),\n"                                                                                                                              \
    "    vec2(0.0, 0.0),\n"                                                                                                                              \
    "    // vertical view (from top)\n"                                                                                                                  \
    "    vec2(0.0, 1.0),\n"                                                                                                                              \
    "    vec2(-1.0, 0.0),\n"                                                                                                                             \
    "    vec2(0.0, 0.0),\n"                                                                                                                              \
    "    vec2(1.0, 0.0),\n"                                                                                                                              \
    "    vec2(1.0, 0.0),\n"                                                                                                                              \
    "    vec2(0.0, 1.0),\n"                                                                                                                              \
    "    vec2(-1.0, 0.0),\n"                                                                                                                             \
    "    vec2(0.0, 0.0)\n"                                                                                                                               \
    "    );\n"                                                                                                                                           \
    "  int id = eff_id < 4 ? id_a : id_b;\n"                                                                                                             \
    "  vec3 endpoint_pos = eff_id < 4 ? pos_a : pos_b;\n"                                                                                                \
    "  vec3 height_width_angle = texelFetch(height_width_angle_tex, id).xyz;\n"                                                                          \
    "#ifdef FIX_TWISTING\n"                                                                                                                              \
    "  int closer_id = (dot(camera_position - pos_a, camera_position - pos_a) < dot(camera_position - pos_b, camera_position - pos_b)) ? id_a : id_b;\n" \
    "  vec3 closer_pos = (closer_id == id_a) ? pos_a : pos_b;\n"                                                                                         \
    "  vec3 camera_view_dir = normalize(closer_pos - camera_position);\n"                                                                                \
    "  vec3 closer_height_width_angle = texelFetch(height_width_angle_tex, closer_id).xyz;\n"                                                            \
    "  vec3 diagonal_dir_border = normalize(closer_height_width_angle.x * line_up_dir + closer_height_width_angle.y * line_right_dir);\n"                \
    "#else\n"                                                                                                                                            \
    "  vec3 camera_view_dir = normalize(endpoint_pos - camera_position);\n"                                                                              \
    "  vec3 diagonal_dir_border = normalize(height_width_angle.x * line_up_dir + height_width_angle.y * line_right_dir);\n"                              \
    "#endif\n"                                                                                                                                           \
    "  bool is_vertical_view = abs(dot(camera_view_dir, line_up_dir)) / abs(dot(diagonal_dir_border, line_up_dir)) >\n"                                  \
    "    abs(dot(camera_view_dir, line_right_dir)) / abs(dot(diagonal_dir_border, line_right_dir));\n"                                                   \
    "  vec2 signs = horizontal_vertical_view_signs_array[eff_id + 8 * int(is_vertical_view)];\n"                                                         \
    "#ifndef POINTY_CAPS\n"                                                                                                                              \
    "  if (eff_id == 2 || eff_id == 7) signs = -horizontal_vertical_view_signs_array[(eff_id - 2) + 8 * int(is_vertical_view)];\n"                       \
    "#endif\n"                                                                                                                                           \
    "  float view_right_sign = dot(-camera_view_dir, line_right_dir) >= 0.0 ? 1.0 : -1.0;\n"                                                             \
    "  float view_top_sign = dot(-camera_view_dir, line_up_dir) >= 0.0 ? 1.0 : -1.0;\n"                                                                  \
    "  float half_height = 0.5 * height_width_angle.x;\n"                                                                                                \
    "  float half_width = 0.5 * height_width_angle.y;\n"                                                                                                 \
    "  vec3 horizontal_dir = half_width * line_right_dir;\n"                                                                                             \
    "  vec3 vertical_dir = half_height * line_up_dir;\n"                                                                                                 \
    "  float horizontal_sign = signs.x * view_right_sign;\n"                                                                                             \
    "  float vertical_sign = signs.y * view_top_sign;\n"                                                                                                 \
    "  vec3 pos = endpoint_pos + horizontal_sign * horizontal_dir + vertical_sign * vertical_dir;\n"                                                     \
    "  if (eff_id == 2 || eff_id == 7) {\n"                                                                                                              \
    "    float line_dir_sign = (eff_id == 2) ? -1.0 : 1.0;\n"                                                                                            \
    "    if (height_width_angle.z == 0) {\n"                                                                                                             \
    "#ifdef POINTY_CAPS\n"                                                                                                                               \
    "      // There I add a cap to lines that do not have a following line\n"                                                                            \
    "      // (or they have one, but perfectly aligned, so the cap is hidden inside the next line).\n"                                                   \
    "      pos += line_dir_sign * line_dir * half_width;\n"                                                                                              \
    "#endif\n"                                                                                                                                           \
    "    }\n"                                                                                                                                            \
    "    else {\n"                                                                                                                                       \
    "      pos += line_dir_sign * line_dir * half_width * sin(abs(height_width_angle.z) * 0.5);\n"                                                       \
    "      pos += sign(height_width_angle.z) * horizontal_dir * cos(abs(height_width_angle.z) * 0.5);\n"                                                 \
    "    }\n"                                                                                                                                            \
    "  }\n"                                                                                                                                              \
    "  // Clip plane cap projection\n"                                                                                                                   \
    "  bool cap_projected = false;\n"                                                                                                                    \
    "  if (is_cap) {\n"                                                                                                                                  \
    "    // Cap corner properties: x=end(0=front,1=back), y=h_sign, z=v_sign\n"                                                                          \
    "    const vec3 cpr[8] = vec3[](vec3(0,1,0),vec3(0,0,1),vec3(0,0,-1),\n"                                                                             \
    "      vec3(1,1,0),vec3(1,0,1),vec3(1,0,-1),vec3(0,-1,0),vec3(1,-1,0));\n"                                                                           \
    "    vec3 ci = cpr[vertex_id - 8];\n"                                                                                                                \
    "    bool cap_front = (ci.x == 0.0);\n"                                                                                                              \
    "    float ch = ci.y;\n"                                                                                                                             \
    "    float cv = ci.z;\n"                                                                                                                             \
    "    vec3 cap_ep = cap_front ? pos_a : pos_b;\n"                                                                                                     \
    "    float dist_a = dot(vec4(pos_a, 1.0), clipping_plane);\n"                                                                                        \
    "    float dist_b = dot(vec4(pos_b, 1.0), clipping_plane);\n"                                                                                        \
    "    bool ep_clipped = cap_front ? (dist_a < 0.0) : (dist_b < 0.0);\n"                                                                               \
    "    if (dist_a * dist_b < 0.0 && ep_clipped) {\n"                                                                                                   \
    "      float t = dist_a / (dist_a - dist_b);\n"                                                                                                      \
    "      vec3 clip_pt = mix(pos_a, pos_b, t);\n"                                                                                                       \
    "      vec3 hwa = mix(texelFetch(height_width_angle_tex, id_a).xyz,\n"                                                                               \
    "                     texelFetch(height_width_angle_tex, id_b).xyz, t);\n"                                                                           \
    "      vec3 corner = clip_pt\n"                                                                                                                      \
    "          + ch * view_right_sign * 0.5 * hwa.y * line_right_dir\n"                                                                                  \
    "          + cv * view_top_sign * 0.5 * hwa.x * line_up_dir;\n"                                                                                      \
    "      float dn = dot(line_dir, clipping_plane.xyz);\n"                                                                                              \
    "      if (abs(dn) > 1e-6) {\n"                                                                                                                      \
    "        float dc = dot(vec4(corner, 1.0), clipping_plane);\n"                                                                                       \
    "        float shift = dc / dn;\n"                                                                                                                   \
    "        // A segment nearly parallel to the clip plane would slide the cap corner\n"                                                                \
    "        // far along the line and draw a ray; the corner already lies within a\n"                                                                   \
    "        // line width of the plane, so keep the cap local instead.\n"                                                                               \
    "        float max_shift = 4.0 * (hwa.x + hwa.y);\n"                                                                                                 \
    "        if (abs(shift) <= max_shift)\n"                                                                                                             \
    "          corner -= shift * line_dir;\n"                                                                                                            \
    "      }\n"                                                                                                                                          \
    "      pos = corner + 0.01 * normalize(clipping_plane.xyz);\n"                                                                                       \
    "      cap_projected = true;\n"                                                                                                                      \
    "    } else {\n"                                                                                                                                     \
    "      pos = cap_ep;\n"                                                                                                                              \
    "    }\n"                                                                                                                                            \
    "  }\n"                                                                                                                                              \
    "  vec3 eye_position = (view_matrix * vec4(pos, 1.0)).xyz;\n"                                                                                        \
    "  vec3 seg_eye_normal;\n"                                                                                                                           \
    "  if (cap_projected)\n"                                                                                                                             \
    "    seg_eye_normal = normalize((view_matrix * vec4(clipping_plane.xyz, 0.0)).xyz);\n"                                                               \
    "  else\n"                                                                                                                                           \
    "    seg_eye_normal = (view_matrix * vec4(normalize(pos - endpoint_pos), 0.0)).xyz;\n"                                                               \
    "  clipping_dist = dot(vec4(pos, 1.0), clipping_plane);\n"                                                                                           \
    "  gl_Position = projection_matrix * vec4(eye_position, 1.0);\n"

static const char *Segments_Vertex_Shader = VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VS_DECLS
    "uniform mat4 shadow_vp;\n"
    "out vec3 color;\n"
    "out vec4 shadow_coord;\n" VGCODE_SEGMENTS_VS_BODY
    "  vec3 color_base = decode_color(texelFetch(color_tex, id).r);\n"
    "  color = color_base * lighting(eye_position, seg_eye_normal);\n"
    "  shadow_coord = shadow_vp * vec4(pos, 1.0);\n"
    "}\n";

static const char *Segments_Fragment_Shader = VGCODE_GLSL_VERSION
    "in vec3 color;\n"
    "in float clipping_dist;\n"
    "in vec4 shadow_coord;\n"
    "uniform int scene_passes;\n"
    "uniform sampler2DShadow shadow_tex;\n"
    "uniform sampler2D ao_tex;\n"
    "uniform vec2 viewport_size;\n"
    "out vec4 fragment_color;\n"
    "void main() {\n"
    "  if (clipping_dist < 0.0) discard;\n"
    "  vec3 rgb = color;\n"
    "  // Full lighting tier: modulate the baked lighting by the world-anchored\n"
    "  // shadow map and screen-space ambient occlusion.\n"
    "  if (scene_passes != 0) {\n"
    "    float shadow = 1.0;\n"
    "    vec4 sc = shadow_coord;\n"
    "    if (sc.w > 0.0) {\n"
    "      sc.z -= 0.0018 * sc.w;\n"
    "      float sum = textureProjOffset(shadow_tex, sc, ivec2(-1, -1));\n"
    "      sum += textureProjOffset(shadow_tex, sc, ivec2(1, -1));\n"
    "      sum += textureProj(shadow_tex, sc);\n"
    "      sum += textureProjOffset(shadow_tex, sc, ivec2(-1, 1));\n"
    "      sum += textureProjOffset(shadow_tex, sc, ivec2(1, 1));\n"
    "      shadow = sum / 5.0;\n"
    "    }\n"
    "    float ao = texture(ao_tex, gl_FragCoord.xy / viewport_size).r;\n"
    "    rgb *= (0.55 + 0.45 * shadow) * (0.45 + 0.55 * ao);\n"
    "  }\n"
    "  fragment_color = vec4(rgb, 1.0);\n"
    "}\n";

// Scene-pass G-buffer variant: eye-space normal plus positive linear eye depth.
static const char *Segments_GBuffer_Vertex_Shader = VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VS_DECLS
    "out vec3 gbuf_eye_normal;\n"
    "out float gbuf_eye_depth;\n" VGCODE_SEGMENTS_VS_BODY "  gbuf_eye_normal = seg_eye_normal;\n"
    "  gbuf_eye_depth = -eye_position.z;\n"
    "}\n";

static const char *Segments_GBuffer_Fragment_Shader = VGCODE_GLSL_VERSION
    "in float clipping_dist;\n"
    "in vec3 gbuf_eye_normal;\n"
    "in float gbuf_eye_depth;\n"
    "out vec4 fragment_color;\n"
    "void main() {\n"
    "  if (clipping_dist < 0.0) discard;\n"
    "  fragment_color = vec4(normalize(gbuf_eye_normal), gbuf_eye_depth);\n"
    "}\n";

static const char *Options_Vertex_Shader = VGCODE_GLSL_VERSION
    "const vec3  light_top_dir = vec3(-0.4574957, 0.4574957, 0.7624929);\n"
    "const float light_top_diffuse = 0.6 * 0.8;\n"
    "const float light_top_specular = 0.6 * 0.125;\n"
    "const float light_top_shininess = 20.0;\n"
    "const vec3  light_front_dir = vec3(0.6985074, 0.1397015, 0.6985074);\n"
    "const float light_front_diffuse = 0.6 * 0.3;\n"
    "const float ambient = 0.3;\n"
    "const float emission = 0.25;\n"
    "const float scaling_factor = 1.5;\n"
    "uniform mat4 view_matrix;\n"
    "uniform mat4 projection_matrix;\n"
    "uniform vec4 clipping_plane;\n"
    "uniform samplerBuffer position_tex;\n"
    "uniform samplerBuffer height_width_angle_tex;\n"
    "uniform samplerBuffer color_tex;\n"
    "uniform usamplerBuffer segment_index_tex;\n"
    "in vec3 in_position;\n"
    "in vec3 in_normal;\n"
    "out vec3 color;\n"
    "out float clipping_dist;\n"
    "vec3 decode_color(float color) {\n"
    "  int c = int(round(color));\n"
    "  int r = (c >> 16) & 0xFF;\n"
    "  int g = (c >> 8) & 0xFF;\n"
    "  int b = (c >> 0) & 0xFF;\n"
    "  float f = 1.0 / 255.0f;\n"
    "  return f * vec3(r, g, b);\n"
    "}\n"
    "float lighting(vec3 eye_position, vec3 eye_normal) {\n"
    "  float top_diffuse = light_top_diffuse * max(dot(eye_normal, light_top_dir), 0.0);\n"
    "  float front_diffuse = light_front_diffuse * max(dot(eye_normal, light_front_dir), 0.0);\n"
    "  float top_specular = light_top_specular * pow(max(dot(-normalize(eye_position), reflect(-light_top_dir, eye_normal)), 0.0), light_top_shininess);\n"
    "  return ambient + top_diffuse + front_diffuse + top_specular + emission;\n"
    "}\n"
    "void main() {\n"
    "  int id = int(texelFetch(segment_index_tex, gl_InstanceID).r);\n"
    "  vec2 height_width = texelFetch(height_width_angle_tex, id).xy;\n"
    "  vec3 offset = texelFetch(position_tex, id).xyz - vec3(0.0, 0.0, 0.5 * height_width.x);\n"
    "  height_width *= scaling_factor;\n"
    "  mat3 scale_matrix = mat3(\n"
    "    height_width.y, 0.0, 0.0,\n"
    "    0.0, height_width.y, 0.0,\n"
    "    0.0, 0.0, height_width.x);\n"
    "  vec3 world_pos = scale_matrix * in_position + offset;\n"
    "  vec3 eye_position = (view_matrix * vec4(world_pos, 1.0)).xyz;\n"
    "  vec3 eye_normal = (view_matrix * vec4(in_normal, 0.0)).xyz;\n"
    "  vec3 color_base = decode_color(texelFetch(color_tex, id).r);\n"
    "  color = color_base * lighting(eye_position, eye_normal);\n"
    "  clipping_dist = dot(vec4(world_pos, 1.0), clipping_plane);\n"
    "  gl_Position = projection_matrix * vec4(eye_position, 1.0);\n"
    "}\n";

static const char *Options_Fragment_Shader = VGCODE_GLSL_VERSION "in vec3 color;\n"
                                                                 "in float clipping_dist;\n"
                                                                 "out vec4 fragment_color;\n"
                                                                 "void main() {\n"
                                                                 "  if (clipping_dist < 0.0) discard;\n"
                                                                 "  fragment_color = vec4(color, 1.0);\n"
                                                                 "}\n";

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
static const char *Cog_Marker_Vertex_Shader = VGCODE_GLSL_VERSION
    "const vec3  light_top_dir = vec3(-0.4574957, 0.4574957, 0.7624929);\n"
    "const float light_top_diffuse = 0.6 * 0.8;\n"
    "const float light_top_specular = 0.6 * 0.125;\n"
    "const float light_top_shininess = 20.0;\n"
    "const vec3  light_front_dir = vec3(0.6985074, 0.1397015, 0.6985074);\n"
    "const float light_front_diffuse = 0.6 * 0.3;\n"
    "const float ambient = 0.3;\n"
    "const float emission = 0.25;\n"
    "uniform vec3 world_center_position;\n"
    "uniform float scale_factor;\n"
    "uniform mat4 view_matrix;\n"
    "uniform mat4 projection_matrix;\n"
    "in vec3 in_position;\n"
    "in vec3 in_normal;\n"
    "out float intensity;\n"
    "out vec3 world_position;\n"
    "float lighting(vec3 eye_position, vec3 eye_normal) {\n"
    "  float top_diffuse = light_top_diffuse * max(dot(eye_normal, light_top_dir), 0.0);\n"
    "  float front_diffuse = light_front_diffuse * max(dot(eye_normal, light_front_dir), 0.0);\n"
    "  float top_specular = light_top_specular * pow(max(dot(-normalize(eye_position), reflect(-light_top_dir, eye_normal)), 0.0), light_top_shininess);\n"
    "  return ambient + top_diffuse + front_diffuse + top_specular + emission;\n"
    "}\n"
    "void main() {\n"
    "  world_position = scale_factor * in_position + world_center_position;\n"
    "  vec3 eye_position = (view_matrix * vec4(world_position, 1.0)).xyz;\n"
    "  vec3 eye_normal = (view_matrix * vec4(in_normal, 0.0)).xyz;\n"
    "  intensity = lighting(eye_position, eye_normal);\n"
    "  gl_Position = projection_matrix * vec4(eye_position, 1.0);\n"
    "}\n";

static const char *Cog_Marker_Fragment_Shader = VGCODE_GLSL_VERSION
    "const vec3 BLACK = vec3(0.05);\n"
    "const vec3 WHITE = vec3(0.95);\n"
    "uniform vec3 world_center_position;\n"
    "in float intensity;\n"
    "in vec3 world_position;\n"
    "out vec4 out_color;\n"
    "void main()\n"
    "{\n"
    "  vec3 delta = world_position - world_center_position;\n"
    "  vec3 color = delta.x * delta.y * delta.z > 0.0 ? BLACK : WHITE;\n"
    "  out_color = intensity * vec4(color, 1.0);\n"
    "}\n";

static const char *Tool_Marker_Vertex_Shader = VGCODE_GLSL_VERSION
    "const vec3  light_top_dir = vec3(-0.4574957, 0.4574957, 0.7624929);\n"
    "const float light_top_diffuse = 0.6 * 0.8;\n"
    "const float light_top_specular = 0.6 * 0.125;\n"
    "const float light_top_shininess = 20.0;\n"
    "const vec3  light_front_dir = vec3(0.6985074, 0.1397015, 0.6985074);\n"
    "const float light_front_diffuse = 0.6 * 0.3;\n"
    "const float ambient = 0.3;\n"
    "const float emission = 0.25;\n"
    "uniform vec3 world_origin;\n"
    "uniform float scale_factor;\n"
    "uniform mat4 view_matrix;\n"
    "uniform mat4 projection_matrix;\n"
    "uniform vec4 color_base;\n"
    "in vec3 in_position;\n"
    "in vec3 in_normal;\n"
    "out vec4 color;\n"
    "float lighting(vec3 eye_position, vec3 eye_normal) {\n"
    "  float top_diffuse = light_top_diffuse * max(dot(eye_normal, light_top_dir), 0.0);\n"
    "  float front_diffuse = light_front_diffuse * max(dot(eye_normal, light_front_dir), 0.0);\n"
    "  float top_specular = light_top_specular * pow(max(dot(-normalize(eye_position), reflect(-light_top_dir, eye_normal)), 0.0), light_top_shininess);\n"
    "  return ambient + top_diffuse + front_diffuse + top_specular + emission;\n"
    "}\n"
    "void main() {\n"
    "  vec3 world_position = scale_factor * in_position + world_origin;\n"
    "  vec3 eye_position = (view_matrix * vec4(world_position, 1.0)).xyz;\n"
    "  // no need of normal matrix as the scaling is uniform\n"
    "  vec3 eye_normal = (view_matrix * vec4(in_normal, 0.0)).xyz;\n"
    "  color = vec4(color_base.rgb * lighting(eye_position, eye_normal), color_base.a);\n"
    "  gl_Position = projection_matrix * vec4(eye_position, 1.0);\n"
    "}\n";

static const char *Tool_Marker_Fragment_Shader = VGCODE_GLSL_VERSION "in vec4 color;\n"
                                                                     "out vec4 fragment_color;\n"
                                                                     "void main() {\n"
                                                                     "  fragment_color = color;\n"
                                                                     "}\n";
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

} // namespace libvgcode
