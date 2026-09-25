#version 140

// Darkens the bed where objects cast shadows or sit close to it (screen-space AO).
// Rendered with alpha blending over the finished bed; outputs black with a
// darkening alpha so every bed style (texture, model, flat) receives shadows.

#define SHADOW_DARKENING 0.30
#define AO_DARKENING     0.38
#define MAX_DARKENING    0.55

uniform sampler2DShadow shadow_tex;
uniform sampler2D ao_tex;
uniform vec2 viewport_size;

in vec4 shadow_coord;

out vec4 out_color;

float shadow_factor()
{
    vec4 sc = shadow_coord;
    if (sc.w <= 0.0)
        return 1.0;
    sc.z -= 0.0018 * sc.w;
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
    float shadow = shadow_factor();
    float ao = texture(ao_tex, gl_FragCoord.xy / viewport_size).r;
    float darkening = SHADOW_DARKENING * (1.0 - shadow) + AO_DARKENING * (1.0 - ao);
    out_color = vec4(0.0, 0.0, 0.0, clamp(darkening, 0.0, MAX_DARKENING));
}
