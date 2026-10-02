#version 140

// Downsamples the supersampled scene to the native framebuffer with a box filter:
// each native pixel averages the target texels its footprint covers, weighted by
// the overlap on each axis. The footprint comes from the real target-to-native
// ratio per axis, since the target size is rounded from the nominal scale.
// Integer arithmetic keeps the weights exact: in units of 1/N texel, native pixel p
// spans [p*W, (p+1)*W) and texel t spans [t*N, (t+1)*N).

uniform sampler2D color_tex;
uniform vec2 native_size;
uniform ivec2 native_origin;

out vec4 out_color;

// Scales up to 4 cover at most 5 texels per axis
const int MAX_TAPS = 5;

void main()
{
    ivec2 tex_size = textureSize(color_tex, 0);
    ivec2 px_size = ivec2(native_size + 0.5);
    ivec2 px = clamp(ivec2(floor(gl_FragCoord.xy)) - native_origin, ivec2(0), px_size - 1);
    ivec2 span_lo = px * tex_size;
    ivec2 span_hi = span_lo + tex_size;
    ivec2 first = span_lo / px_size;

    vec4 sum = vec4(0.0);
    for (int j = 0; j < MAX_TAPS; ++j)
    {
        int ty = first.y + j;
        int oy = min(span_hi.y, (ty + 1) * px_size.y) - max(span_lo.y, ty * px_size.y);
        if (oy > 0)
        {
            float wy = float(oy) / float(tex_size.y);
            for (int i = 0; i < MAX_TAPS; ++i)
            {
                int tx = first.x + i;
                int ox = min(span_hi.x, (tx + 1) * px_size.x) - max(span_lo.x, tx * px_size.x);
                if (ox > 0)
                    sum += texelFetch(color_tex, ivec2(tx, ty), 0) * (float(ox) / float(tex_size.x) * wy);
            }
        }
    }
    out_color = sum;
}
