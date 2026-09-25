#version 140

// Screen-space ambient occlusion over the scene G-buffer.
// G-buffer: rgb = eye-space normal, a = positive linear eye depth (0 = background).

uniform sampler2D gbuffer_tex;
uniform mat4 projection_matrix;
uniform mat4 inv_projection_matrix;
uniform float ao_radius;    // sampling radius in scene units (mm)
uniform float ao_bias;      // depth bias to avoid self-occlusion
uniform float ao_intensity; // occlusion strength multiplier

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

// Reconstruct the eye-space position of a pixel from its stored linear depth.
// Works for both perspective and orthographic projections: eye-space points along
// a pixel's ray are linear in eye z for either camera type.
vec3 reconstruct_eye_pos(vec2 uv, float depth)
{
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 p_near = inv_projection_matrix * vec4(ndc, -1.0, 1.0);
    vec4 p_far  = inv_projection_matrix * vec4(ndc,  1.0, 1.0);
    p_near /= p_near.w;
    p_far  /= p_far.w;
    float zn = -p_near.z;
    float zf = -p_far.z;
    float t = (depth - zn) / max(zf - zn, 0.000001);
    return vec3(mix(p_near.xy, p_far.xy, t), -depth);
}

void main()
{
    vec4 g = texture(gbuffer_tex, tex_coord);
    float depth = g.a;
    if (depth <= 0.0)
    {
        out_color = vec4(1.0);
        return;
    }

    vec3 normal = normalize(g.rgb);
    vec3 eye_pos = reconstruct_eye_pos(tex_coord, depth);

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

        float sample_depth = texture(gbuffer_tex, sample_uv).a;
        if (sample_depth <= 0.0)
            continue;

        float range_check = smoothstep(0.0, 1.0, ao_radius / abs(depth - sample_depth));
        occlusion += (sample_depth < -sample_pos.z - ao_bias ? 1.0 : 0.0) * range_check;
    }

    occlusion = 1.0 - ao_intensity * (occlusion / float(KERNEL_SIZE));
    out_color = vec4(vec3(clamp(occlusion, 0.0, 1.0)), 1.0);
}
