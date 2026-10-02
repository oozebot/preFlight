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
    "  // The rasterizer clips at the plane (GL_CLIP_DISTANCE0 is on while a plane is set); a fragment discard\n"                                        \
    "  // instead would turn off hidden surface removal on tile-based GPUs.\n"                                                                           \
    "  gl_ClipDistance[0] = dot(vec4(pos, 1.0), clipping_plane);\n"                                                                                      \
    "  gl_Position = projection_matrix * vec4(eye_position, 1.0);\n"

// Pieces of the visible segments program shared by the plain program and the toolpath prefilter variant: the
// vertex shader's outputs, the wall neighbour decode, its colour, its shadow coordinate and weight, and its closing
// brace; the fragment shader's inputs, its head up to the baked colour and its Full lighting tier composite. The
// shadow statements follow the colour, ahead of the prefilter's, so the eye-space position and normal they read are
// not held through the prefilter statements.
#define VGCODE_SEGMENTS_VISIBLE_VS_DECLS                                                                 \
    "uniform mat4 shadow_vp;\n"                                                                          \
    "// The Full lighting tier's switch, shared with the fragment shader.\n"                             \
    "uniform int scene_passes;\n"                                                                        \
    "// Distance (mm) past a wall bead's side at which its shadow lookup is taken.\n"                    \
    "uniform float shadow_offset_margin;\n"                                                              \
    "out vec3 color;\n"                                                                                  \
    "out vec4 shadow_coord;\n"                                                                           \
    "// The key light's share of the baked lighting, the part the shadow dims.\n"                        \
    "out float shadow_weight;\n"                                                                         \
    "// Neighbour data of a vertex, in the w of its heights_widths_angles texel (256 times the flags\n"  \
    "// plus the y offset) and of its position texel (the x offset). Flags: 1 the vertex is on a wall\n" \
    "// segment, 2 the bead above or below it was found. Offset: the XY step (mm) from the vertex to\n"  \
    "// the bead above.\n"                                                                               \
    "int pf_flags_of(int vid) {\n"                                                                       \
    "  float w = texelFetch(height_width_angle_tex, vid).w;\n"                                           \
    "  return int(floor((w + 128.0) / 256.0));\n"                                                        \
    "}\n"                                                                                                \
    "vec3 pf_offset_of(int vid) {\n"                                                                     \
    "  float w = texelFetch(height_width_angle_tex, vid).w;\n"                                           \
    "  return vec3(texelFetch(position_tex, vid).w, w - 256.0 * floor((w + 128.0) / 256.0), 0.0);\n"     \
    "}\n"

#define VGCODE_SEGMENTS_VISIBLE_VS_COLOR                               \
    "  vec3 color_base = decode_color(texelFetch(color_tex, id).r);\n" \
    "  color = color_base * lighting(eye_position, seg_eye_normal);\n"

#define VGCODE_SEGMENTS_VISIBLE_VS_SHADOW                                                                             \
    "  // The shadow coordinate and weight are read only with the Full lighting tier on.\n"                           \
    "  shadow_coord = vec4(0.0);\n"                                                                                   \
    "  shadow_weight = 0.0;\n"                                                                                        \
    "  if (scene_passes != 0) {\n"                                                                                    \
    "    // The shadow pass draws each bead facing the light and this pass facing the camera, so a wall\n"            \
    "    // bead's visible surface lies behind the light's version of it and shadows itself layer by layer.\n"        \
    "    // Its shadow lookup is taken off the bead surface: from the centre line, pushed out along the\n"            \
    "    // wall's normal on the side the camera sees by half the bead width plus a margin, the wall's\n"             \
    "    // slope from the offset to the bead above. That normal also gives the wall's key light share.\n"            \
    "    vec3 sh_pos = pos;\n"                                                                                        \
    "    vec3 sh_eye_n = seg_eye_normal;\n"                                                                           \
    "    if (!is_cap && abs(dot(line_dir, UP)) <= 0.1 && (pf_flags_of(id) & 1) != 0) {\n"                             \
    "      vec3 sh_up = height_width_angle.x * UP + (((pf_flags_of(id) & 2) != 0) ? pf_offset_of(id) : vec3(0.0));\n" \
    "      vec3 sh_n = normalize(cross(line_dir, sh_up));\n"                                                          \
    "      if (dot(sh_n, view_right_sign * line_right_dir) < 0.0) sh_n = -sh_n;\n"                                    \
    "      sh_pos = endpoint_pos + sh_n * (0.5 * height_width_angle.y + shadow_offset_margin);\n"                     \
    "      sh_eye_n = (view_matrix * vec4(sh_n, 0.0)).xyz;\n"                                                         \
    "    }\n"                                                                                                         \
    "    shadow_coord = shadow_vp * vec4(sh_pos, 1.0);\n"                                                             \
    "    // As on meshes, the shadow dims only the key light's diffuse term. It fades out where the light\n"          \
    "    // grazes a face, so a cast shadow's edge, stepped by the map's texels there, does not show.\n"              \
    "    sh_eye_n = normalize(sh_eye_n);\n"                                                                           \
    "    float sh_key = light_top_diffuse * max(dot(sh_eye_n, light_top_dir), 0.0);\n"                                \
    "    shadow_weight = sh_key / lighting(eye_position, sh_eye_n);\n"                                                \
    "  }\n"

#define VGCODE_SEGMENTS_VISIBLE_VS_END "}\n"

#define VGCODE_SEGMENTS_VISIBLE_FS_INPUTS \
    "in vec3 color;\n"                    \
    "in vec4 shadow_coord;\n"             \
    "in float shadow_weight;\n"

#define VGCODE_SEGMENTS_VISIBLE_FS_BEGIN    \
    "uniform int scene_passes;\n"           \
    "uniform sampler2DShadow shadow_tex;\n" \
    "uniform sampler2D ao_tex;\n"           \
    "uniform vec2 viewport_size;\n"         \
    "uniform vec2 viewport_origin;\n"       \
    "out vec4 fragment_color;\n"            \
    "void main() {\n"                       \
    "  vec3 rgb = color;\n"

#define VGCODE_SEGMENTS_VISIBLE_FS_END                                                                 \
    "  // Full lighting tier: the world-anchored shadow map dims the key light's share of the baked\n" \
    "  // lighting, and screen-space ambient occlusion the whole of it.\n"                             \
    "  if (scene_passes != 0) {\n"                                                                     \
    "    float shadow = 1.0;\n"                                                                        \
    "    vec4 sc = shadow_coord;\n"                                                                    \
    "    if (sc.w > 0.0) {\n"                                                                          \
    "      sc.z -= 0.0018 * sc.w;\n"                                                                   \
    "      float sum = textureProjOffset(shadow_tex, sc, ivec2(-1, -1));\n"                            \
    "      sum += textureProjOffset(shadow_tex, sc, ivec2(1, -1));\n"                                  \
    "      sum += textureProj(shadow_tex, sc);\n"                                                      \
    "      sum += textureProjOffset(shadow_tex, sc, ivec2(-1, 1));\n"                                  \
    "      sum += textureProjOffset(shadow_tex, sc, ivec2(1, 1));\n"                                   \
    "      shadow = sum / 5.0;\n"                                                                      \
    "    }\n"                                                                                          \
    "    float ao = texture(ao_tex, (gl_FragCoord.xy - viewport_origin) / viewport_size).r;\n"         \
    "    rgb *= (1.0 - shadow_weight * (1.0 - shadow)) * (0.45 + 0.55 * ao);\n"                        \
    "  }\n"                                                                                            \
    "  fragment_color = vec4(rgb, 1.0);\n"                                                             \
    "}\n"

static const char *Segments_Vertex_Shader =
    VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VS_DECLS VGCODE_SEGMENTS_VISIBLE_VS_DECLS VGCODE_SEGMENTS_VS_BODY
        VGCODE_SEGMENTS_VISIBLE_VS_COLOR VGCODE_SEGMENTS_VISIBLE_VS_SHADOW VGCODE_SEGMENTS_VISIBLE_VS_END;

static const char *Segments_Fragment_Shader = VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VISIBLE_FS_INPUTS
    VGCODE_SEGMENTS_VISIBLE_FS_BEGIN VGCODE_SEGMENTS_VISIBLE_FS_END;

// Visible segments program with the toolpath prefilter. Wall beads are shaded with the box average, across the line,
// of their bead's lighting profile repeated layer after layer, so layers a few pixels tall do not alias into moire:
// near level for every wall bead, and up to steep views from above for beads whose two ends both found a wall
// neighbour one layer up or down at load. The viewer draws with it only in frames where some visible toolpath can be
// seen in that range; its vertex and fragment shaders are the plain program's plus the prefilter statements.
static const char *Segments_PF_Vertex_Shader =
    VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VS_DECLS VGCODE_SEGMENTS_VISIBLE_VS_DECLS
    "uniform vec2 pf_viewport_px;\n"
    "uniform float pf_width;\n"
    "uniform vec2 pf_fade_elevation;\n"
    "uniform vec2 pf_fade_pitch;\n"
    "// Positions across the line in output px, times clip w so that perspective-correct interpolation\n"
    "// rebuilds them at the depth of the fragment: the near outer, ridge and far outer corner (xyz), this vertex (w).\n"
    "out vec4 pf_tf;\n"
    "// Lighting of the near outer, ridge and far outer corner (xyz); the step to the next layer, px times clip w (w).\n"
    "out vec4 pf_ls;\n"
    "// Base colour (rgb) and filter strength (w).\n"
    "out vec4 pf_bf;\n"
    "vec3 pf_px(vec3 world) {\n"
    "  vec4 c = projection_matrix * (view_matrix * vec4(world, 1.0));\n"
    "  return vec3(0.5 * pf_viewport_px * c.xy / c.w, c.w);\n"
    "}\n"
    "float pf_light(vec3 p, vec3 dir) {\n"
    "  return lighting((view_matrix * vec4(p, 1.0)).xyz, (view_matrix * vec4(normalize(dir), 0.0)).xyz);\n"
    "}\n"
    "// Camera elevations (degrees) above a bead with a found neighbour where its strength is full and where it is off.\n"
    "uniform vec2 pf_fade_above;\n"
    "// Bead tops of the top and bottom displayed layers.\n"
    "uniform float pf_top_z;\n"
    "uniform float pf_bottom_z;\n" VGCODE_SEGMENTS_VS_BODY
        VGCODE_SEGMENTS_VISIBLE_VS_COLOR VGCODE_SEGMENTS_VISIBLE_VS_SHADOW "  pf_tf = vec4(0.0);\n"
    "  pf_ls = vec4(0.0);\n"
    "  pf_bf = vec4(color_base, 0.0);\n"
    "  // The flags both segment ends share, so both ends carry the profile data and the fade, or neither does.\n"
    "  int nf0 = pf_flags_of(id_a) & pf_flags_of(id_b);\n"
    "  if (pf_width > 0.0 && !is_cap && abs(dot(line_dir, UP)) <= 0.1 && (nf0 & 1) != 0) {\n"
    "    float elev = degrees(asin(clamp(dot(-camera_view_dir, UP), -1.0, 1.0)));\n"
    "    // A found neighbour gives the true step to the next layer on screen from any view, so such a bead seen\n"
    "    // from above is filtered up to steep views; the others, and every bead seen from below, only near level.\n"
    "    bool found = (nf0 & 2) != 0;\n"
    "    float fade = (found && elev >= 0.0) ? 1.0 - smoothstep(pf_fade_above.x, pf_fade_above.y, elev) : 1.0 - smoothstep(pf_fade_elevation.x, pf_fade_elevation.y, abs(elev));\n"
    "    vec3 c0 = pf_px(endpoint_pos);\n"
    "    vec3 ca = pf_px(pos_a);\n"
    "    vec3 cb = pf_px(pos_b);\n"
    "    vec3 cv = pf_px(pos);\n"
    "    if (fade > 0.02 && c0.z > 0.0 && ca.z > 0.0 && cb.z > 0.0 && cv.z > 0.0) {\n"
    "      vec2 dl = cb.xy - ca.xy;\n"
    "      float dlen = length(dl);\n"
    "      vec2 dn = dlen > 1e-4 ? dl / dlen : vec2(1.0, 0.0);\n"
    "      vec2 nn = vec2(-dn.y, dn.x);\n"
    "      // The profile runs across the imposter: top, side and bottom corner in the side view, one side, the top\n"
    "      // and the other side in the roof view.\n"
    "      vec3 outer_off = is_vertical_view ? view_right_sign * horizontal_dir : view_top_sign * vertical_dir;\n"
    "      vec3 mid_off = is_vertical_view ? view_top_sign * vertical_dir : view_right_sign * horizontal_dir;\n"
    "      vec3 pa = endpoint_pos + outer_off;\n"
    "      vec3 pb = endpoint_pos - outer_off;\n"
    "      vec3 pm = endpoint_pos + mid_off;\n"
    "      float ta = dot(pf_px(pa).xy - c0.xy, nn);\n"
    "      float tb = dot(pf_px(pb).xy - c0.xy, nn);\n"
    "      float tm = dot(pf_px(pm).xy - c0.xy, nn);\n"
    "      float la = pf_light(pa, outer_off);\n"
    "      float lb = pf_light(pb, -outer_off);\n"
    "      float lm = pf_light(pm, mid_off);\n"
    "      // A face turned away from the camera is hidden: its outer corner collapses onto the ridge corner,\n"
    "      // so the profile holds only the faces the camera sees.\n"
    "      vec3 fa = cross(pm - pa, line_dir);\n"
    "      if (dot(fa, 0.5 * (pa + pm) - endpoint_pos) < 0.0) fa = -fa;\n"
    "      vec3 fb = cross(pb - pm, line_dir);\n"
    "      if (dot(fb, 0.5 * (pb + pm) - endpoint_pos) < 0.0) fb = -fb;\n"
    "      bool front_a = dot(fa, camera_position - 0.5 * (pa + pm)) > 0.0;\n"
    "      bool front_b = dot(fb, camera_position - 0.5 * (pb + pm)) > 0.0;\n"
    "      if (!front_a && front_b) { ta = tm; la = lm; }\n"
    "      if (!front_b && front_a) { tb = tm; lb = lm; }\n"
    "      // A bead is covered by the next layer, one layer height up plus the found offset (down when the camera\n"
    "      // is below the bead), unless it lies in the top displayed layer (the bottom one, camera below).\n"
    "      bool cam_above = camera_position.z >= endpoint_pos.z;\n"
    "      float bead_top = endpoint_pos.z + 0.5 * height_width_angle.x;\n"
    "      bool covered = cam_above ? bead_top < pf_top_z - 0.25 * height_width_angle.x : bead_top > pf_bottom_z + 0.25 * height_width_angle.x;\n"
    "      vec3 stp = height_width_angle.x * UP + (found ? pf_offset_of(id) : vec3(0.0));\n"
    "      if (!cam_above) stp = -stp;\n"
    "      vec3 cs = pf_px(endpoint_pos + stp);\n"
    "      float s = (covered && cs.z > 0.0) ? dot(cs.xy - c0.xy, nn) : 1e6;\n"
    "      pf_tf = vec4(vec3(ta, tm, tb) * c0.z, dot(cv.xy - c0.xy, nn) * cv.z);\n"
    "      pf_ls = vec4(la, lm, lb, s * c0.z);\n"
    "      // Strength fades out with the view elevation of the bead and with the on-screen layer pitch.\n"
    "      pf_bf.w = fade * (1.0 - smoothstep(pf_fade_pitch.x, pf_fade_pitch.y, abs(s)));\n"
    "    }\n"
    "  }\n" VGCODE_SEGMENTS_VISIBLE_VS_END;

static const char *Segments_PF_Fragment_Shader = VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VISIBLE_FS_INPUTS
    "uniform float pf_width;\n"
    "in vec4 pf_tf;\n"
    "in vec4 pf_ls;\n"
    "in vec4 pf_bf;\n"
    "// Antiderivative from t.x of the profile through (t.x,l.x), (t.y,l.y), (t.z,l.z) with slopes k, u in [t.x, t.z].\n"
    "float pf_g(float u, vec3 t, vec3 l, vec2 k) {\n"
    "  float a = min(u, t.y) - t.x;\n"
    "  float g = a * (l.x + 0.5 * k.x * a);\n"
    "  float b = max(u - t.y, 0.0);\n"
    "  return g + b * (l.y + 0.5 * k.y * b);\n"
    "}\n" VGCODE_SEGMENTS_VISIBLE_FS_BEGIN
    "  // Box filter: the lighting averaged over pf_width px across the line, of the bead profile whose visible\n"
    "  // span [band.x, band.y] repeats every band.z px (the next layer hides the rest of the bead).\n"
    "  if (pf_width * pf_bf.w > 0.05) {\n"
    "    float inv_w = gl_FragCoord.w;\n"
    "    vec3 t = pf_tf.xyz * inv_w;\n"
    "    vec3 l = pf_ls.xyz;\n"
    "    float ft = pf_tf.w * inv_w;\n"
    "    float s = pf_ls.w * inv_w;\n"
    "    if (t.x > t.y) { t.xy = t.yx; l.xy = l.yx; }\n"
    "    if (t.y > t.z) { t.yz = t.zy; l.yz = l.zy; }\n"
    "    if (t.x > t.y) { t.xy = t.yx; l.xy = l.yx; }\n"
    "    vec3 band = vec3(t.x, t.z, 1e6);\n"
    "    // A covered bead's span repeats at its step. An uncovered bead's step of 1e6 px fades its strength to 0, so it\n"
    "    // is not filtered; between a covered and an uncovered end the interpolated step can still pass 1e5 px, and the\n"
    "    // bead is then averaged alone.\n"
    "    if (abs(s) < 1e5) {\n"
    "      float v0 = s >= 0.0 ? t.x : max(t.x, t.z + s);\n"
    "      float v1 = s >= 0.0 ? min(t.z, t.x + s) : t.z;\n"
    "      band = vec3(v0, max(v1, v0 + 1e-4), max(max(abs(s), v1 - v0), 1e-3));\n"
    "    }\n"
    "    // A fragment out of reach of the visible span (the top layer, a silhouette) averages its bead alone.\n"
    "    if (ft > band.y + 0.5 * pf_width + 0.5 || ft < band.x - 0.5 * pf_width - 0.5)\n"
    "      band = vec3(t.x, t.z, 1e6);\n"
    "    vec2 k = vec2(t.y - t.x > 1e-6 ? (l.y - l.x) / (t.y - t.x) : 0.0, t.z - t.y > 1e-6 ? (l.z - l.y) / (t.z - t.y) : 0.0);\n"
    "    // With the span mean m, the box average is m plus the change over the box of the periodic remainder\n"
    "    // D(x) = G(band.x + x) - G(band.x) - m x, which is 0 past the span.\n"
    "    float bw = band.y - band.x;\n"
    "    float g0 = pf_g(band.x, t, l, k);\n"
    "    float m = (pf_g(band.y, t, l, k) - g0) / bw;\n"
    "    float w = min(pf_width * pf_bf.w, band.z);\n"
    "    float x2 = mod(ft + 0.5 * w - band.x, band.z);\n"
    "    float x1 = mod(ft - 0.5 * w - band.x, band.z);\n"
    "    float d2 = x2 <= bw ? pf_g(band.x + x2, t, l, k) - g0 - m * x2 : 0.0;\n"
    "    float d1 = x1 <= bw ? pf_g(band.x + x1, t, l, k) - g0 - m * x1 : 0.0;\n"
    "    rgb = pf_bf.rgb * (m + (d2 - d1) / w);\n"
    "  }\n" VGCODE_SEGMENTS_VISIBLE_FS_END;

// Scene-pass G-buffer variant: the eye-space normal encoded to [0, 1]; depth comes from the target's depth
// attachment. Alpha marks geometry (the target clears to 0): toolpath pixels are marked 2/3 so SSAO can tell them
// from meshes and the bed, which write 1; a >= 0.5 still tests for geometry, and RGB10_A2 stores 2/3 exactly.
static const char *Segments_GBuffer_Vertex_Shader = VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VS_DECLS
    "out vec3 gbuf_eye_normal;\n" VGCODE_SEGMENTS_VS_BODY "  gbuf_eye_normal = seg_eye_normal;\n"
    "}\n";

static const char *Segments_GBuffer_Fragment_Shader = VGCODE_GLSL_VERSION
    "in vec3 gbuf_eye_normal;\n"
    "out vec4 fragment_color;\n"
    "void main() {\n"
    "  fragment_color = vec4(normalize(gbuf_eye_normal) * 0.5 + 0.5, 2.0 / 3.0);\n"
    "}\n";

// Shadow map pass: depth only. The imposter geometry and clip distance of the visible program, no colour, lighting,
// shadow or prefilter work; the shadow target has no colour attachment.
static const char *Segments_Depth_Vertex_Shader = VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VS_DECLS VGCODE_SEGMENTS_VS_BODY
    "}\n";

static const char *Segments_Depth_Fragment_Shader = VGCODE_GLSL_VERSION "void main() {\n"
                                                                        "}\n";

// Visibility probe: the depth-only geometry writing each fragment's id (read per drawn instance from
// instance_id_tex) plus one (0 is the target's clear value, no segment) to an unsigned integer target.
static const char *Segments_Id_Vertex_Shader = VGCODE_GLSL_VERSION VGCODE_SEGMENTS_VS_DECLS
    "uniform usamplerBuffer instance_id_tex;\n"
    "flat out uint seg_id;\n" VGCODE_SEGMENTS_VS_BODY "  seg_id = texelFetch(instance_id_tex, gl_InstanceID).r;\n"
    "}\n";

static const char *Segments_Id_Fragment_Shader = VGCODE_GLSL_VERSION "flat in uint seg_id;\n"
                                                                     "out uint fragment_id;\n"
                                                                     "void main() {\n"
                                                                     "  fragment_id = seg_id + 1u;\n"
                                                                     "}\n";

// Occlusion culling: the fullscreen triangle of the depth pyramid's passes and of the depth merge, its corners in
// attribute 0
static const char *Occlusion_Fullscreen_Vertex_Shader = VGCODE_GLSL_VERSION "in vec2 corner;\n"
                                                                            "void main() {\n"
                                                                            "  gl_Position = vec4(corner, 0.0, 1.0);\n"
                                                                            "}\n";

// Occlusion culling: level 0 of the depth pyramid, each texel the depth target's
static const char *Occlusion_Copy_Fragment_Shader = VGCODE_GLSL_VERSION
    "uniform sampler2D depth_tex;\n"
    "out vec4 result;\n"
    "void main() {\n"
    "  result = vec4(texelFetch(depth_tex, ivec2(gl_FragCoord.xy), 0).r);\n"
    "}\n";

// Occlusion culling: level L + 1 of the depth pyramid from level L, which the sampled texture's base and maximum level
// select (lod 0 reads it). Each texel holds the largest of the source texels in columns 2x and 2x + 1, through the
// source's last column for the last column, and in rows 2y and 2y + 1, through the source's last row for the last row;
// a first index past the source is clamped to its last (build_depth_pyramid's rule).
static const char *Occlusion_Reduce_Fragment_Shader = VGCODE_GLSL_VERSION
    "uniform sampler2D source_tex;\n"
    "uniform ivec2 source_size;\n"
    "uniform ivec2 target_size;\n"
    "out vec4 result;\n"
    "void main() {\n"
    "  ivec2 t = ivec2(gl_FragCoord.xy);\n"
    "  int col_first = min(2 * t.x, source_size.x - 1);\n"
    "  int col_last = t.x == target_size.x - 1 ? source_size.x - 1 : 2 * t.x + 1;\n"
    "  int row_first = min(2 * t.y, source_size.y - 1);\n"
    "  int row_last = t.y == target_size.y - 1 ? source_size.y - 1 : 2 * t.y + 1;\n"
    "  float farthest = texelFetch(source_tex, ivec2(col_first, row_first), 0).r;\n"
    "  // At most 3 columns and 3 rows\n"
    "  for (int j = 0; j < 3; ++j)\n"
    "    for (int i = 0; i < 3; ++i) {\n"
    "      ivec2 s = ivec2(col_first + i, row_first + j);\n"
    "      if (s.x <= col_last && s.y <= row_last)\n"
    "        farthest = max(farthest, texelFetch(source_tex, s, 0).r);\n"
    "    }\n"
    "  result = vec4(farthest);\n"
    "}\n";

// Occlusion culling: the faces test of one box per point, test_box_occlusion_faces (OcclusionTest.cpp) step for step
// with its constants and face table as uniforms, and margins of its own so that its rounding never makes it stricter
// than that rule: the window rectangle widened by window_margin pixels on every side (for the outside test, the texel
// range and the footprints), depth_tolerance also below the near plane and past the far plane. Point i writes 1
// (visible) or 0 to texel (i % result_width, i / result_width) of the result target. Stage 0 tests the chunk boxes,
// each only when its byte at chunk_flag_offset + i in flags_tex (it holds an enabled sub-cell) is set; stage 1 the
// sub-cell boxes, each only when its own flag and its chunk's result are set. A box at the largest float on an axis,
// or NaN, is unbounded and passes.
static const char *Occlusion_Test_Vertex_Shader = VGCODE_GLSL_VERSION
    "uniform mat4 view_proj;\n"
    "uniform vec2 viewport_size;\n"
    "uniform ivec2 viewport_texels;\n"
    "uniform float orientation;\n"
    "uniform int last_level;\n"
    "uniform int taps;\n"
    "uniform int stage;\n"
    "uniform int result_width;\n"
    "uniform vec2 result_size;\n"
    "uniform int chunk_flag_offset;\n"
    "uniform float min_clip_w;\n"
    "uniform float depth_tolerance;\n"
    "uniform float face_min_area_fraction;\n"
    "uniform float face_min_area;\n"
    "uniform float window_margin;\n"
    "uniform float unbounded;\n"
    "uniform ivec4 box_faces[6];\n"
    "uniform sampler2D pyramid_tex;\n"
    "uniform sampler2D chunk_result_tex;\n"
    "uniform usamplerBuffer flags_tex;\n"
    "in vec3 box_min;\n"
    "in vec3 box_max;\n"
    "in int chunk_index;\n"
    "flat out float visible;\n"
    "bool faces_visible() {\n"
    "  if (!(box_min.x > -unbounded && box_min.y > -unbounded && box_min.z > -unbounded && box_max.x < unbounded &&\n"
    "        box_max.y < unbounded && box_max.z < unbounded))\n"
    "    return true;\n"
    "  // window_corners: corner c takes the max on axis a when bit a of c is set; visible when a corner's\n"
    "  // clip w is not above min_clip_w or a window value is not finite\n"
    "  float wx[8];\n"
    "  float wy[8];\n"
    "  float wd[8];\n"
    "  for (int c = 0; c < 8; ++c) {\n"
    "    vec3 p = vec3((c & 1) != 0 ? box_max.x : box_min.x, (c & 2) != 0 ? box_max.y : box_min.y,\n"
    "                  (c & 4) != 0 ? box_max.z : box_min.z);\n"
    "    vec4 clip = view_proj[0] * p.x + view_proj[1] * p.y + view_proj[2] * p.z + view_proj[3];\n"
    "    if (!(clip.w > min_clip_w))\n"
    "      return true;\n"
    "    wx[c] = (clip.x / clip.w * 0.5 + 0.5) * viewport_size.x;\n"
    "    wy[c] = (clip.y / clip.w * 0.5 + 0.5) * viewport_size.y;\n"
    "    wd[c] = clip.z / clip.w * 0.5 + 0.5;\n"
    "    if (isnan(wx[c]) || isinf(wx[c]) || isnan(wy[c]) || isinf(wy[c]) || isnan(wd[c]) || isinf(wd[c]))\n"
    "      return true;\n"
    "  }\n"
    "  // footprint_of_corners: the rectangle and the nearest depth, raised to 0; off the viewport or beyond\n"
    "  // the far plane is outside\n"
    "  float min_x = wx[0];\n"
    "  float max_x = wx[0];\n"
    "  float min_y = wy[0];\n"
    "  float max_y = wy[0];\n"
    "  float nearest = wd[0];\n"
    "  for (int c = 1; c < 8; ++c) {\n"
    "    min_x = min(min_x, wx[c]);\n"
    "    max_x = max(max_x, wx[c]);\n"
    "    min_y = min(min_y, wy[c]);\n"
    "    max_y = max(max_y, wy[c]);\n"
    "    nearest = min(nearest, wd[c]);\n"
    "  }\n"
    "  nearest = max(nearest, 0.0);\n"
    "  min_x -= window_margin;\n"
    "  max_x += window_margin;\n"
    "  min_y -= window_margin;\n"
    "  max_y += window_margin;\n"
    "  if (max_x < 0.0 || min_x > viewport_size.x || max_y < 0.0 || min_y > viewport_size.y ||\n"
    "      nearest > 1.0 + depth_tolerance)\n"
    "    return false;\n"
    "  float rx0 = clamp(min_x, 0.0, viewport_size.x);\n"
    "  float rx1 = clamp(max_x, 0.0, viewport_size.x);\n"
    "  float ry0 = clamp(min_y, 0.0, viewport_size.y);\n"
    "  float ry1 = clamp(max_y, 0.0, viewport_size.y);\n"
    "  int x0 = clamp(int(floor(rx0)), 0, viewport_texels.x - 1);\n"
    "  int x1 = max(clamp(int(ceil(rx1)) - 1, 0, viewport_texels.x - 1), x0);\n"
    "  int y0 = clamp(int(floor(ry0)), 0, viewport_texels.y - 1);\n"
    "  int y1 = max(clamp(int(ceil(ry1)) - 1, 0, viewport_texels.y - 1), y0);\n"
    "  // Nearer than the near plane\n"
    "  for (int c = 0; c < 8; ++c)\n"
    "    if (wd[c] < depth_tolerance)\n"
    "      return true;\n"
    "  // front_faces, for a box with min <= max on every axis: a face is front when its window area, signed\n"
    "  // by the orientation, is above the edge-on limit; its plane is fitted to the larger of its triangles\n"
    "  // p0 p1 p2 and p0 p2 p3\n"
    "  bool proper = box_min.x <= box_max.x && box_min.y <= box_max.y && box_min.z <= box_max.z;\n"
    "  float min_area2 = 2.0 * max(face_min_area_fraction * (max_x - min_x) * (max_y - min_y), face_min_area);\n"
    "  bool front[6];\n"
    "  float plane_x0[6];\n"
    "  float plane_y0[6];\n"
    "  float plane_d0[6];\n"
    "  float plane_a[6];\n"
    "  float plane_b[6];\n"
    "  for (int k = 0; k < 6; ++k) {\n"
    "    front[k] = false;\n"
    "    plane_x0[k] = 0.0;\n"
    "    plane_y0[k] = 0.0;\n"
    "    plane_d0[k] = 0.0;\n"
    "    plane_a[k] = 0.0;\n"
    "    plane_b[k] = 0.0;\n"
    "    if (!proper || orientation == 0.0)\n"
    "      continue;\n"
    "    ivec4 q = box_faces[k];\n"
    "    float fx = wx[q.x];\n"
    "    float fy = wy[q.x];\n"
    "    float fd = wd[q.x];\n"
    "    vec3 e0 = vec3(wx[q.y] - fx, wy[q.y] - fy, wd[q.y] - fd);\n"
    "    vec3 e1 = vec3(wx[q.z] - fx, wy[q.z] - fy, wd[q.z] - fd);\n"
    "    vec3 e2 = vec3(wx[q.w] - fx, wy[q.w] - fy, wd[q.w] - fd);\n"
    "    float t1 = e0.x * e1.y - e0.y * e1.x;\n"
    "    float t2 = e1.x * e2.y - e1.y * e2.x;\n"
    "    if (!(orientation * (t1 + t2) > min_area2))\n"
    "      continue;\n"
    "    bool first = orientation * t1 >= orientation * t2;\n"
    "    vec3 u = first ? e0 : e1;\n"
    "    vec3 v = first ? e1 : e2;\n"
    "    float d = first ? t1 : t2;\n"
    "    front[k] = true;\n"
    "    plane_x0[k] = fx;\n"
    "    plane_y0[k] = fy;\n"
    "    plane_d0[k] = fd;\n"
    "    plane_a[k] = (u.z * v.y - u.y * v.z) / d;\n"
    "    plane_b[k] = (u.x * v.z - u.z * v.x) / d;\n"
    "  }\n"
    "  // pyramid_texels: the smallest level where the range spans at most taps texels per axis, the last level else\n"
    "  int span = max(taps, 1) - 1;\n"
    "  int level = 0;\n"
    "  int tx0 = x0;\n"
    "  int tx1 = x1;\n"
    "  int ty0 = y0;\n"
    "  int ty1 = y1;\n"
    "  for (int l = 0; l < 32; ++l) {\n"
    "    if (l > last_level)\n"
    "      break;\n"
    "    int lw = max(1, viewport_texels.x >> l);\n"
    "    int lh = max(1, viewport_texels.y >> l);\n"
    "    level = l;\n"
    "    tx0 = min(x0 >> l, lw - 1);\n"
    "    tx1 = min(x1 >> l, lw - 1);\n"
    "    ty0 = min(y0 >> l, lh - 1);\n"
    "    ty1 = min(y1 >> l, lh - 1);\n"
    "    if (tx1 - tx0 <= span && ty1 - ty0 <= span)\n"
    "      break;\n"
    "  }\n"
    "  int level_w = max(1, viewport_texels.x >> level);\n"
    "  int level_h = max(1, viewport_texels.y >> level);\n"
    "  // Per texel: its footprint in the rectangle, each front plane at its nearest corner of it, against\n"
    "  // the texel's farthest depth; at most 8 texels per axis\n"
    "  for (int j = 0; j < 8; ++j) {\n"
    "    int y = ty0 + j;\n"
    "    if (y > ty1)\n"
    "      break;\n"
    "    float fy0 = max(float(y << level), ry0);\n"
    "    float fy1 = min(y == level_h - 1 ? viewport_size.y : float((y + 1) << level), ry1);\n"
    "    if (fy0 > fy1)\n"
    "      continue;\n"
    "    for (int i = 0; i < 8; ++i) {\n"
    "      int x = tx0 + i;\n"
    "      if (x > tx1)\n"
    "        break;\n"
    "      float fx0 = max(float(x << level), rx0);\n"
    "      float fx1 = min(x == level_w - 1 ? viewport_size.x : float((x + 1) << level), rx1);\n"
    "      if (fx0 > fx1)\n"
    "        continue;\n"
    "      float bound = nearest;\n"
    "      for (int k = 0; k < 6; ++k) {\n"
    "        if (!front[k])\n"
    "          continue;\n"
    "        float depth = plane_d0[k] + plane_a[k] * ((plane_a[k] > 0.0 ? fx0 : fx1) - plane_x0[k]) +\n"
    "                      plane_b[k] * ((plane_b[k] > 0.0 ? fy0 : fy1) - plane_y0[k]);\n"
    "        bound = max(bound, depth);\n"
    "      }\n"
    "      if (bound <= texelFetch(pyramid_tex, ivec2(x, y), level).r + depth_tolerance)\n"
    "        return true;\n"
    "    }\n"
    "  }\n"
    "  return false;\n"
    "}\n"
    "void main() {\n"
    "  int i = gl_VertexID;\n"
    "  bool candidate;\n"
    "  if (stage == 0)\n"
    "    candidate = texelFetch(flags_tex, chunk_flag_offset + i).r != 0u;\n"
    "  else\n"
    "    candidate = texelFetch(flags_tex, i).r != 0u &&\n"
    "                texelFetch(chunk_result_tex, ivec2(chunk_index % result_width, chunk_index / result_width),\n"
    "                           0).r > 0.5;\n"
    "  visible = (candidate && faces_visible()) ? 1.0 : 0.0;\n"
    "  vec2 pixel = vec2(float(i % result_width), float(i / result_width)) + 0.5;\n"
    "  gl_Position = vec4(2.0 * pixel / result_size - 1.0, 0.0, 1.0);\n"
    "  gl_PointSize = 1.0;\n"
    "}\n";

static const char *Occlusion_Test_Fragment_Shader = VGCODE_GLSL_VERSION "flat in float visible;\n"
                                                                        "out vec4 result;\n"
                                                                        "void main() {\n"
                                                                        "  result = vec4(visible);\n"
                                                                        "}\n";

// Occlusion culling: the depth target's value as the fragment's depth, for the caller's depth-only target; the target
// texel is the fragment's position within the caller's viewport (its lower left corner at viewport_origin)
static const char *Occlusion_Merge_Fragment_Shader = VGCODE_GLSL_VERSION
    "uniform sampler2D depth_tex;\n"
    "uniform ivec2 viewport_origin;\n"
    "void main() {\n"
    "  gl_FragDepth = texelFetch(depth_tex, ivec2(gl_FragCoord.xy) - viewport_origin, 0).r;\n"
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
    "  // The rasterizer clips at the plane (GL_CLIP_DISTANCE0 is on while a plane is set)\n"
    "  gl_ClipDistance[0] = dot(vec4(world_pos, 1.0), clipping_plane);\n"
    "  gl_Position = projection_matrix * vec4(eye_position, 1.0);\n"
    "}\n";

static const char *Options_Fragment_Shader = VGCODE_GLSL_VERSION "in vec3 color;\n"
                                                                 "out vec4 fragment_color;\n"
                                                                 "void main() {\n"
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
