#version 140

// Screen-space ambient occlusion over the scene G-buffer.
// gbuffer_tex: rgb = eye-space normal mapped to [0, 1]; a = 1 for meshes and the bed, 2/3 for toolpaths,
// 0 for background (a >= 0.5 is geometry).
// depth_tex: the G-buffer's depth attachment, from which eye-space positions are rebuilt. It clears to 1.0 and
// every G-buffer draw writes depth and alpha >= 0.5 together under GL_LESS, so a texel is background exactly when
// its depth is 1.0 (the 24-bit maximum reads back as exactly 1.0).

uniform sampler2D gbuffer_tex;
uniform sampler2D depth_tex;
uniform mat4 projection_matrix;
uniform mat4 inv_projection_matrix;
uniform float ao_radius;    // sampling radius in scene units (mm)
uniform float ao_bias;      // depth bias to avoid self-occlusion
uniform float ao_intensity; // occlusion strength multiplier
// Toolpath pixels orient the hemisphere by a normal rebuilt from depth this many G-buffer texels to each
// side; 0 keeps the G-buffer normal everywhere.
uniform float ao_toolpath_normal_step;

in vec2 tex_coord;

out vec4 out_color;

const int KERNEL_SIZE = 16;
// Hemisphere kernel (z >= 0), progressively scaled toward the center.
const vec3 SSAO_KERNEL[KERNEL_SIZE] = vec3[](
    vec3( 0.0721,  0.0362,  0.0512), vec3(-0.0603,  0.0834,  0.0713),
    vec3( 0.0295, -0.1122,  0.0918), vec3(-0.1301, -0.0571,  0.1123),
    vec3( 0.1670,  0.0823,  0.0761), vec3( 0.0632,  0.1857,  0.1341),
    vec3(-0.2011,  0.1163,  0.1622), vec3(-0.0842, -0.2360,  0.1520),
    vec3( 0.2513, -0.1614,  0.1841), vec3( 0.3170,  0.1421,  0.2265),
    vec3(-0.3521,  0.2011,  0.1420), vec3(-0.1721, -0.3814,  0.2513),
    vec3( 0.1311,  0.4522,  0.2810), vec3( 0.4820, -0.2513,  0.3122),
    vec3(-0.5211, -0.3010,  0.3520), vec3( 0.3521, -0.5222,  0.4211));

// Eye-space position of the point at uv with window depth `window_depth`, through the
// inverse projection; holds for both perspective and orthographic cameras.
vec3 reconstruct_eye_pos(vec2 uv, float window_depth)
{
    vec4 p = inv_projection_matrix * vec4(uv * 2.0 - 1.0, window_depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

void main()
{
    vec4 g = texture(gbuffer_tex, tex_coord);
    if (g.a < 0.5)
    {
        out_color = vec4(1.0);
        return;
    }

    vec3 normal = normalize(g.rgb * 2.0 - 1.0);
    vec3 eye_pos = reconstruct_eye_pos(tex_coord, texture(depth_tex, tex_coord).r);

    if (ao_toolpath_normal_step > 0.0 && g.a < 0.9)
    {
        // A toolpath's G-buffer normal is its bead's ridge face, which tilts every layer; the surface rebuilt
        // from depth follows the wall instead. Per axis, the side with the smaller depth step among the
        // neighbours that are geometry, so a silhouette does not tilt it; with no such neighbour on an axis
        // the G-buffer normal stays.
        vec2 texel_dn = 1.0 / vec2(textureSize(depth_tex, 0));
        vec2 ox = vec2(ao_toolpath_normal_step, 0.0) * texel_dn;
        vec2 oy = vec2(0.0, ao_toolpath_normal_step) * texel_dn;
        float z_r = texture(depth_tex, tex_coord + ox).r;
        float z_l = texture(depth_tex, tex_coord - ox).r;
        float z_u = texture(depth_tex, tex_coord + oy).r;
        float z_d = texture(depth_tex, tex_coord - oy).r;
        vec3 p_r = reconstruct_eye_pos(tex_coord + ox, z_r);
        vec3 p_l = reconstruct_eye_pos(tex_coord - ox, z_l);
        vec3 p_u = reconstruct_eye_pos(tex_coord + oy, z_u);
        vec3 p_d = reconstruct_eye_pos(tex_coord - oy, z_d);
        // Geometry is any depth below the cleared 1.0
        bool r_ok = z_r < 1.0;
        bool l_ok = z_l < 1.0;
        bool u_ok = z_u < 1.0;
        bool d_ok = z_d < 1.0;
        if ((r_ok || l_ok) && (u_ok || d_ok))
        {
            bool use_r = r_ok && (!l_ok || abs(p_r.z - eye_pos.z) < abs(eye_pos.z - p_l.z));
            bool use_u = u_ok && (!d_ok || abs(p_u.z - eye_pos.z) < abs(eye_pos.z - p_d.z));
            vec3 ddx = use_r ? p_r - eye_pos : eye_pos - p_l;
            vec3 ddy = use_u ? p_u - eye_pos : eye_pos - p_d;
            vec3 n = normalize(cross(ddx, ddy));
            normal = dot(n, -eye_pos) < 0.0 ? -n : n;
        }
    }

    // Positive linear eye depth
    float depth = -eye_pos.z;

    // Per-pixel random rotation without a noise texture (interleaved gradient noise).
    float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float ca = cos(angle);
    float sa = sin(angle);

    // Build an orthonormal basis around the normal, rotated per pixel.
    vec3 helper = abs(normal.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    vec3 t_rot = tangent * ca + bitangent * sa;
    vec3 b_rot = -tangent * sa + bitangent * ca;
    mat3 tbn = mat3(t_rot, b_rot, normal);

    float occlusion = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i)
    {
        vec3 sample_pos = eye_pos + tbn * SSAO_KERNEL[i] * ao_radius;

        vec4 clip = projection_matrix * vec4(sample_pos, 1.0);
        vec2 sample_uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (any(lessThan(sample_uv, vec2(0.0))) || any(greaterThan(sample_uv, vec2(1.0))))
            continue;

        // One read per sample: the cleared depth marks background
        float sample_window_depth = texture(depth_tex, sample_uv).r;
        if (sample_window_depth >= 1.0)
            continue;
        float sample_depth = -reconstruct_eye_pos(sample_uv, sample_window_depth).z;

        float range_check = smoothstep(0.0, 1.0, ao_radius / abs(depth - sample_depth));
        occlusion += (sample_depth < -sample_pos.z - ao_bias ? 1.0 : 0.0) * range_check;
    }

    occlusion = 1.0 - ao_intensity * (occlusion / float(KERNEL_SIZE));
    out_color = vec4(vec3(clamp(occlusion, 0.0, 1.0)), 1.0);
}
