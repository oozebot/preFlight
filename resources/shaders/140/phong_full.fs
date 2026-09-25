#version 140

// Full lighting tier: shadow-mapped key light, screen-space ambient occlusion,
// Cook-Torrance specular, spherical-harmonics environment ambient.

#define KEY_LIGHT_INTENSITY  1.9
#define FILL_LIGHT_INTENSITY 0.35
#define INTENSITY_AMBIENT    0.30

#define RIM_POWER            3.0
#define RIM_INTENSITY        0.10

// Eye-space fill light (matches the Enhanced tier's front light).
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);

// Spherical harmonics coefficients for a neutral studio environment (order-2 SH).
const vec3 SH_L00  = vec3( 0.38,  0.36,  0.34);
const vec3 SH_L1m1 = vec3( 0.02,  0.01, -0.01);
const vec3 SH_L10  = vec3( 0.12,  0.12,  0.14);
const vec3 SH_L11  = vec3(-0.04, -0.03, -0.02);
const vec3 SH_L2m2 = vec3(-0.01, -0.01, -0.01);
const vec3 SH_L2m1 = vec3( 0.03,  0.03,  0.04);
const vec3 SH_L20  = vec3( 0.06,  0.06,  0.08);
const vec3 SH_L21  = vec3(-0.01, -0.01, -0.01);
const vec3 SH_L22  = vec3(-0.02, -0.02, -0.02);

#define REFLECTION_STRENGTH  0.22

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
const float EPSILON = 0.0001;
const float PI = 3.14159265;

struct PrintVolumeDetection
{
    int type;
    vec4 xy_data;
    vec2 z_data;
};

struct SlopeDetection
{
    bool actived;
    float normal_z;
    mat3 volume_world_normal_matrix;
    vec3 color;
};

uniform vec4 uniform_color;
uniform bool use_color_clip_plane;
uniform vec4 uniform_color_clip_plane_1;
uniform vec4 uniform_color_clip_plane_2;
uniform SlopeDetection slope;

uniform sampler2DShadow shadow_tex;
uniform sampler2D ao_tex;
uniform vec2 viewport_size;
uniform vec3 key_light_eye;  // world key light direction transformed to eye space
uniform float pbr_roughness;
uniform float pbr_metallic;

#ifdef ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

uniform PrintVolumeDetection print_volume;

in vec3 eye_normal;
in vec3 eye_position;
in vec3 clipping_planes_dots;
in float color_clip_plane_dot;
in vec4 world_pos;
in float world_normal_z;
in vec4 shadow_coord;

out vec4 out_color;

float shadow_factor()
{
    vec4 sc = shadow_coord;
    if (sc.w <= 0.0)
        return 1.0;
    sc.z -= 0.0018 * sc.w; // depth bias against acne
    float sum = 0.0;
    sum += textureProjOffset(shadow_tex, sc, ivec2(-1, -1));
    sum += textureProjOffset(shadow_tex, sc, ivec2( 1, -1));
    sum += textureProj(shadow_tex, sc);
    sum += textureProjOffset(shadow_tex, sc, ivec2(-1,  1));
    sum += textureProjOffset(shadow_tex, sc, ivec2( 1,  1));
    return sum / 5.0;
}

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;

    vec4 color;
    if (use_color_clip_plane) {
        color.rgb = (color_clip_plane_dot < 0.0) ? uniform_color_clip_plane_1.rgb : uniform_color_clip_plane_2.rgb;
        color.a = uniform_color.a;
    }
    else
        color = uniform_color;

    if (slope.actived && world_normal_z < slope.normal_z - EPSILON) {
        color.rgb = slope.color;
        color.a = 1.0;
    }

    // Print volume bounds check
    vec3 pv_check_min = ZERO;
    vec3 pv_check_max = ZERO;
    if (print_volume.type == 0) {
        pv_check_min = world_pos.xyz - vec3(print_volume.xy_data.x, print_volume.xy_data.y, print_volume.z_data.x);
        pv_check_max = world_pos.xyz - vec3(print_volume.xy_data.z, print_volume.xy_data.w, print_volume.z_data.y);
    }
    else if (print_volume.type == 1) {
        float delta_radius = print_volume.xy_data.z - distance(world_pos.xy, print_volume.xy_data.xy);
        pv_check_min = vec3(delta_radius, 0.0, world_pos.z - print_volume.z_data.x);
        pv_check_max = vec3(0.0, 0.0, world_pos.z - print_volume.z_data.y);
    }
    color.rgb = (any(lessThan(pv_check_min, ZERO)) || any(greaterThan(pv_check_max, ZERO))) ? mix(color.rgb, ZERO, 0.3333) : color.rgb;

    vec3 normal = normalize(eye_normal);
    vec3 view_dir = normalize(-eye_position);

    float shadow = shadow_factor();
    float ao = texture(ao_tex, gl_FragCoord.xy / viewport_size).r;

    // Cook-Torrance key light
    vec3 light_dir = normalize(key_light_eye);
    vec3 half_dir = normalize(light_dir + view_dir);
    float NdotL = max(dot(normal, light_dir), 0.0);
    float NdotV = max(dot(normal, view_dir), EPSILON);
    float NdotH = max(dot(normal, half_dir), 0.0);
    float VdotH = max(dot(view_dir, half_dir), 0.0);

    float rough = clamp(pbr_roughness, 0.04, 1.0);
    float a2 = rough * rough * rough * rough;
    float denom_d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom_d * denom_d);

    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));

    vec3 F0 = mix(vec3(0.04), color.rgb, pbr_metallic);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    vec3 specular = (D * G * F) / max(4.0 * NdotL * NdotV, EPSILON);
    vec3 kd = (vec3(1.0) - F) * (1.0 - pbr_metallic);

    vec3 direct = (kd * color.rgb / PI + specular) * KEY_LIGHT_INTENSITY * NdotL * shadow;

    // Unshadowed fill light, diffuse only
    float NdotL_fill = max(dot(normal, LIGHT_FRONT_DIR), 0.0);
    vec3 fill = color.rgb * FILL_LIGHT_INTENSITY * NdotL_fill;

    // Ambient and environment, both attenuated by ambient occlusion
    vec3 ambient = color.rgb * INTENSITY_AMBIENT * ao;

    float rim = pow(max(0.0, 1.0 - dot(view_dir, normal)), RIM_POWER) * RIM_INTENSITY * ao;

    vec3 refl = reflect(-view_dir, normal);
    vec3 sh_color = SH_L00
        + SH_L1m1 * refl.y + SH_L10 * refl.z + SH_L11 * refl.x
        + SH_L2m2 * (refl.x * refl.y) + SH_L2m1 * (refl.y * refl.z)
        + SH_L20 * (3.0 * refl.z * refl.z - 1.0) + SH_L21 * (refl.x * refl.z)
        + SH_L22 * (refl.x * refl.x - refl.y * refl.y);
    vec3 reflection = max(sh_color, vec3(0.0)) * REFLECTION_STRENGTH * ao * mix(vec3(1.0), F0, pbr_metallic);

#ifdef ENABLE_ENVIRONMENT_MAP
    if (use_environment_tex) {
        out_color = vec4(0.45 * texture(environment_tex, normalize(eye_normal).xy * 0.5 + 0.5).xyz + 0.8 * color.rgb * (INTENSITY_AMBIENT + NdotL * shadow), color.a);
        return;
    }
#endif
    out_color = vec4(min(direct + fill + ambient + reflection + vec3(rim), vec3(1.0)), color.a);
}
