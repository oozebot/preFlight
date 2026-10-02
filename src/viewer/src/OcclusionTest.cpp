///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "OcclusionTest.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace libvgcode
{

namespace
{

float texel(const DepthLevel &level, int x, int y)
{
    return level.depth[size_t(y) * size_t(level.width) + size_t(x)];
}

// Level-0 index i at level L of the given size
int index_at(int i, int level, int size)
{
    return std::min(i >> level, size - 1);
}

// Window x, y and depth of the box's 8 corners (corner i takes the max on axis a when bit a of i is set); false when a
// corner is at or behind the eye plane or a value is not finite
bool window_corners(const float view_proj[16], const float box_min[3], const float box_max[3], int width, int height,
                    float wx[8], float wy[8], float wd[8])
{
    const float *m = view_proj;
    for (int i = 0; i < 8; ++i)
    {
        const float p[3] = {(i & 1) != 0 ? box_max[0] : box_min[0], (i & 2) != 0 ? box_max[1] : box_min[1],
                            (i & 4) != 0 ? box_max[2] : box_min[2]};
        float clip[4];
        for (int r = 0; r < 4; ++r)
            clip[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
        // At or behind the eye plane (a NaN w included)
        if (!(clip[3] > OCCLUSION_MIN_CLIP_W))
            return false;
        const float ndc_x = clip[0] / clip[3];
        const float ndc_y = clip[1] / clip[3];
        const float ndc_z = clip[2] / clip[3];
        wx[i] = (ndc_x * 0.5f + 0.5f) * float(width);
        wy[i] = (ndc_y * 0.5f + 0.5f) * float(height);
        wd[i] = ndc_z * 0.5f + 0.5f;
        if (!std::isfinite(wx[i]) || !std::isfinite(wy[i]) || !std::isfinite(wd[i]))
            return false;
    }
    return true;
}

// The footprint of a bounded window rectangle and its nearest depth (raised to 0 here)
BoxFootprint footprint_of_rectangle(float min_x, float max_x, float min_y, float max_y, float nearest, int width,
                                    int height)
{
    BoxFootprint footprint;
    footprint.bounded = true;
    footprint.nearest = std::max(nearest, 0.0f);
    if (max_x < 0.0f || min_x > float(width) || max_y < 0.0f || min_y > float(height) || footprint.nearest > 1.0f)
    {
        footprint.outside = true;
        return footprint;
    }
    footprint.rx0 = std::clamp(min_x, 0.0f, float(width));
    footprint.rx1 = std::clamp(max_x, 0.0f, float(width));
    footprint.ry0 = std::clamp(min_y, 0.0f, float(height));
    footprint.ry1 = std::clamp(max_y, 0.0f, float(height));
    footprint.x0 = std::clamp(int(std::floor(footprint.rx0)), 0, width - 1);
    footprint.x1 = std::max(std::clamp(int(std::ceil(footprint.rx1)) - 1, 0, width - 1), footprint.x0);
    footprint.y0 = std::clamp(int(std::floor(footprint.ry0)), 0, height - 1);
    footprint.y1 = std::max(std::clamp(int(std::ceil(footprint.ry1)) - 1, 0, height - 1), footprint.y0);
    return footprint;
}

// The footprint of a bounded box from its window corners
BoxFootprint footprint_of_corners(const float wx[8], const float wy[8], const float wd[8], int width, int height)
{
    float min_x = wx[0], max_x = wx[0], min_y = wy[0], max_y = wy[0], nearest = wd[0];
    for (int i = 1; i < 8; ++i)
    {
        min_x = std::min(min_x, wx[i]);
        max_x = std::max(max_x, wx[i]);
        min_y = std::min(min_y, wy[i]);
        max_y = std::max(max_y, wy[i]);
        nearest = std::min(nearest, wd[i]);
    }
    return footprint_of_rectangle(min_x, max_x, min_y, max_y, nearest, width, height);
}

// One end of a segment: its centre's window point, its ball's window radius and nearest window depth
struct SegmentEnd
{
    float x, y, radius, nearest;
};

// The end of centre p and radius `radius` (segment_footprint's steps 1 to 3); false when its ball reaches the eye plane
// or a value is not finite
bool segment_end(const float view_proj[16], const float p[3], float radius, int width, int height, SegmentEnd &end)
{
    const float *m = view_proj;
    const float r = std::abs(radius);
    float clip[4];
    for (int i = 0; i < 4; ++i)
        clip[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i];
    // The least clip w over the ball, at or behind the eye plane (a NaN included)
    const float w_min = clip[3] - r * std::sqrt(m[3] * m[3] + m[7] * m[7] + m[11] * m[11]);
    if (!(w_min > OCCLUSION_MIN_CLIP_W))
        return false;
    const float ndc_x = clip[0] / clip[3];
    const float ndc_y = clip[1] / clip[3];
    const float ndc_z = clip[2] / clip[3];
    // An offset v of the centre moves its window x, y and depth by e . v / w, w the clip w of the moved point
    float ex[3], ey[3], ez[3];
    for (int k = 0; k < 3; ++k)
    {
        const float gw = m[4 * k + 3];
        ex[k] = 0.5f * float(width) * (m[4 * k] - ndc_x * gw);
        ey[k] = 0.5f * float(height) * (m[4 * k + 1] - ndc_y * gw);
        ez[k] = 0.5f * (m[4 * k + 2] - ndc_z * gw);
    }
    const float p2 = ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2];
    const float q2 = ey[0] * ey[0] + ey[1] * ey[1] + ey[2] * ey[2];
    const float s = ex[0] * ey[0] + ex[1] * ey[1] + ex[2] * ey[2];
    // The largest eigenvalue of [p2 s; s q2]: the square of the longest window offset per unit of world offset
    const float half_difference = 0.5f * (p2 - q2);
    const float lambda = 0.5f * (p2 + q2) + std::sqrt(half_difference * half_difference + s * s);
    end.x = (ndc_x * 0.5f + 0.5f) * float(width);
    end.y = (ndc_y * 0.5f + 0.5f) * float(height);
    end.radius = r * std::sqrt(lambda) / w_min + OCCLUSION_SEGMENT_MARGIN;
    end.nearest = ndc_z * 0.5f + 0.5f - r * std::sqrt(ez[0] * ez[0] + ez[1] * ez[1] + ez[2] * ez[2]) / w_min;
    return std::isfinite(end.x) && std::isfinite(end.y) && std::isfinite(end.radius) && std::isfinite(end.nearest);
}

bool proper_box(const float box_min[3], const float box_max[3])
{
    return box_min[0] <= box_max[0] && box_min[1] <= box_max[1] && box_min[2] <= box_max[2];
}

// 1 when a front face has a positive window area (view_proj's determinant is negative), -1 when a negative one, 0 for
// a zero or non-finite determinant. The determinant by 2 x 2 minors of columns 0 and 1 against columns 2 and 3.
float face_orientation(const float view_proj[16])
{
    double m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = view_proj[i];
    const double s0 = m[0] * m[5] - m[4] * m[1];
    const double s1 = m[0] * m[6] - m[4] * m[2];
    const double s2 = m[0] * m[7] - m[4] * m[3];
    const double s3 = m[1] * m[6] - m[5] * m[2];
    const double s4 = m[1] * m[7] - m[5] * m[3];
    const double s5 = m[2] * m[7] - m[6] * m[3];
    const double c0 = m[8] * m[13] - m[12] * m[9];
    const double c1 = m[8] * m[14] - m[12] * m[10];
    const double c2 = m[8] * m[15] - m[12] * m[11];
    const double c3 = m[9] * m[14] - m[13] * m[10];
    const double c4 = m[9] * m[15] - m[13] * m[11];
    const double c5 = m[10] * m[15] - m[14] * m[11];
    const double det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
    if (!std::isfinite(det) || det == 0.0)
        return 0.0f;
    return det < 0.0 ? 1.0f : -1.0f;
}

// A front face's plane in window space: d = d0 + a (x - x0) + b (y - y0)
struct FacePlane
{
    float x0, y0, d0, a, b;
};

// The front faces of a box from its window corners: their bits, and their planes in planes[0, count)
uint8_t front_faces(const float wx[8], const float wy[8], const float wd[8], float orientation, FacePlane planes[6],
                    int &count)
{
    count = 0;
    if (orientation == 0.0f)
        return 0;
    float min_x = wx[0], max_x = wx[0], min_y = wy[0], max_y = wy[0];
    for (int i = 1; i < 8; ++i)
    {
        min_x = std::min(min_x, wx[i]);
        max_x = std::max(max_x, wx[i]);
        min_y = std::min(min_y, wy[i]);
        max_y = std::max(max_y, wy[i]);
    }
    // Twice the edge-on limit, compared with twice the area
    const float min_area2 = 2.0f * std::max(OCCLUSION_FACE_MIN_AREA_FRACTION * (max_x - min_x) * (max_y - min_y),
                                            OCCLUSION_FACE_MIN_AREA);
    uint8_t mask = 0;
    for (int k = 0; k < 6; ++k)
    {
        const int *q = OCCLUSION_BOX_FACES[k];
        const float x0 = wx[q[0]];
        const float y0 = wy[q[0]];
        const float d0 = wd[q[0]];
        // The other corners relative to the first: x, y, depth
        float e[3][3];
        for (int j = 0; j < 3; ++j)
        {
            e[j][0] = wx[q[j + 1]] - x0;
            e[j][1] = wy[q[j + 1]] - y0;
            e[j][2] = wd[q[j + 1]] - d0;
        }
        // Twice the areas of the triangles p0 p1 p2 and p0 p2 p3
        const float t1 = e[0][0] * e[1][1] - e[0][1] * e[1][0];
        const float t2 = e[1][0] * e[2][1] - e[1][1] * e[2][0];
        if (!(orientation * (t1 + t2) > min_area2))
            continue;
        // The plane through the larger triangle
        const bool first = orientation * t1 >= orientation * t2;
        const float *u = first ? e[0] : e[1];
        const float *v = first ? e[1] : e[2];
        const float d = first ? t1 : t2;
        FacePlane &plane = planes[count++];
        plane.x0 = x0;
        plane.y0 = y0;
        plane.d0 = d0;
        plane.a = (u[2] * v[1] - u[1] * v[2]) / d;
        plane.b = (u[0] * v[2] - u[2] * v[0]) / d;
        mask = uint8_t(mask | (1u << k));
    }
    return mask;
}

} // namespace

DepthPyramid build_depth_pyramid(int width, int height, std::vector<float> depth)
{
    DepthPyramid pyramid;
    if (width <= 0 || height <= 0 || depth.size() != size_t(width) * size_t(height))
        return pyramid;
    DepthLevel base;
    base.width = width;
    base.height = height;
    base.depth = std::move(depth);
    pyramid.levels.push_back(std::move(base));
    while (pyramid.levels.back().width > 1 || pyramid.levels.back().height > 1)
    {
        const DepthLevel &src = pyramid.levels.back();
        DepthLevel dst;
        dst.width = std::max(1, src.width / 2);
        dst.height = std::max(1, src.height / 2);
        dst.depth.resize(size_t(dst.width) * size_t(dst.height));
        for (int y = 0; y < dst.height; ++y)
        {
            // Rows 2y and 2y + 1, through the source's last row for the last row
            const int row_first = std::min(2 * y, src.height - 1);
            const int row_last = y == dst.height - 1 ? src.height - 1 : 2 * y + 1;
            for (int x = 0; x < dst.width; ++x)
            {
                const int col_first = std::min(2 * x, src.width - 1);
                const int col_last = x == dst.width - 1 ? src.width - 1 : 2 * x + 1;
                float farthest = texel(src, col_first, row_first);
                for (int sy = row_first; sy <= row_last; ++sy)
                    for (int sx = col_first; sx <= col_last; ++sx)
                        farthest = std::max(farthest, texel(src, sx, sy));
                dst.depth[size_t(y) * size_t(dst.width) + size_t(x)] = farthest;
            }
        }
        // src is not used past this point: the push may move the levels
        pyramid.levels.push_back(std::move(dst));
    }
    return pyramid;
}

BoxFootprint box_footprint(const float view_proj[16], const float box_min[3], const float box_max[3], int width,
                           int height)
{
    float wx[8], wy[8], wd[8];
    if (width <= 0 || height <= 0 || !window_corners(view_proj, box_min, box_max, width, height, wx, wy, wd))
        return BoxFootprint();
    return footprint_of_corners(wx, wy, wd, width, height);
}

PyramidTexels pyramid_texels(const DepthPyramid &pyramid, int x0, int y0, int x1, int y1, int taps)
{
    PyramidTexels t;
    t.x0 = x0;
    t.y0 = y0;
    t.x1 = x1;
    t.y1 = y1;
    if (pyramid.levels.empty())
        return t;
    const DepthLevel &base = pyramid.levels.front();
    x0 = std::clamp(x0, 0, base.width - 1);
    x1 = std::clamp(x1, 0, base.width - 1);
    y0 = std::clamp(y0, 0, base.height - 1);
    y1 = std::clamp(y1, 0, base.height - 1);
    const int span = std::max(taps, 1) - 1;
    // The last level is 1 x 1, where every range is one texel
    const int last = int(pyramid.levels.size()) - 1;
    for (int level = 0; level <= last; ++level)
    {
        const DepthLevel &l = pyramid.levels[size_t(level)];
        t.level = level;
        t.x0 = index_at(x0, level, l.width);
        t.x1 = index_at(x1, level, l.width);
        t.y0 = index_at(y0, level, l.height);
        t.y1 = index_at(y1, level, l.height);
        if (t.x1 - t.x0 <= span && t.y1 - t.y0 <= span)
            break;
    }
    return t;
}

float pyramid_farthest(const DepthPyramid &pyramid, int x0, int y0, int x1, int y1)
{
    if (pyramid.levels.empty())
        return 1.0f;
    const PyramidTexels t = pyramid_texels(pyramid, x0, y0, x1, y1);
    const DepthLevel &l = pyramid.levels[size_t(t.level)];
    return std::max(std::max(texel(l, t.x0, t.y0), texel(l, t.x1, t.y0)),
                    std::max(texel(l, t.x0, t.y1), texel(l, t.x1, t.y1)));
}

float exact_farthest(const DepthPyramid &pyramid, int x0, int y0, int x1, int y1)
{
    if (pyramid.levels.empty())
        return 1.0f;
    const DepthLevel &base = pyramid.levels.front();
    x0 = std::clamp(x0, 0, base.width - 1);
    x1 = std::clamp(x1, 0, base.width - 1);
    y0 = std::clamp(y0, 0, base.height - 1);
    y1 = std::clamp(y1, 0, base.height - 1);
    float farthest = texel(base, x0, y0);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            farthest = std::max(farthest, texel(base, x, y));
    return farthest;
}

BoxVisibility test_box_occlusion(const float view_proj[16], const float box_min[3], const float box_max[3],
                                 const DepthPyramid &pyramid)
{
    if (pyramid.levels.empty())
        return BoxVisibility::Visible;
    const DepthLevel &base = pyramid.levels.front();
    const BoxFootprint f = box_footprint(view_proj, box_min, box_max, base.width, base.height);
    if (!f.bounded)
        return BoxVisibility::Visible;
    if (f.outside)
        return BoxVisibility::Outside;
    const float occluder = pyramid_farthest(pyramid, f.x0, f.y0, f.x1, f.y1);
    return f.nearest <= occluder + OCCLUSION_DEPTH_TOLERANCE ? BoxVisibility::Visible : BoxVisibility::Occluded;
}

BoxVisibility test_box_occlusion_exact(const float view_proj[16], const float box_min[3], const float box_max[3],
                                       const DepthPyramid &pyramid)
{
    if (pyramid.levels.empty())
        return BoxVisibility::Visible;
    const DepthLevel &base = pyramid.levels.front();
    const BoxFootprint f = box_footprint(view_proj, box_min, box_max, base.width, base.height);
    if (!f.bounded)
        return BoxVisibility::Visible;
    if (f.outside)
        return BoxVisibility::Outside;
    // The first texel that passes decides, which equals comparing with the largest (adding the tolerance keeps order)
    for (int y = f.y0; y <= f.y1; ++y)
        for (int x = f.x0; x <= f.x1; ++x)
            if (f.nearest <= texel(base, x, y) + OCCLUSION_DEPTH_TOLERANCE)
                return BoxVisibility::Visible;
    return BoxVisibility::Occluded;
}

uint8_t box_front_faces(const float view_proj[16], const float box_min[3], const float box_max[3], int width,
                        int height)
{
    float wx[8], wy[8], wd[8];
    if (width <= 0 || height <= 0 || !proper_box(box_min, box_max) ||
        !window_corners(view_proj, box_min, box_max, width, height, wx, wy, wd))
        return 0;
    FacePlane planes[6];
    int count = 0;
    return front_faces(wx, wy, wd, face_orientation(view_proj), planes, count);
}

BoxVisibility test_box_occlusion_faces(const float view_proj[16], const float box_min[3], const float box_max[3],
                                       const DepthPyramid &pyramid, int taps)
{
    if (pyramid.levels.empty())
        return BoxVisibility::Visible;
    const DepthLevel &base = pyramid.levels.front();
    float wx[8], wy[8], wd[8];
    if (!window_corners(view_proj, box_min, box_max, base.width, base.height, wx, wy, wd))
        return BoxVisibility::Visible;
    const BoxFootprint f = footprint_of_corners(wx, wy, wd, base.width, base.height);
    if (f.outside)
        return BoxVisibility::Outside;
    // Nearer than the near plane
    for (const float d : wd)
        if (d < 0.0f)
            return BoxVisibility::Visible;
    // An inverted box would take its back faces for front ones: it keeps the nearest depth alone
    FacePlane planes[6];
    int count = 0;
    if (proper_box(box_min, box_max))
        front_faces(wx, wy, wd, face_orientation(view_proj), planes, count);

    const PyramidTexels t = pyramid_texels(pyramid, f.x0, f.y0, f.x1, f.y1, taps);
    const DepthLevel &l = pyramid.levels[size_t(t.level)];
    for (int y = t.y0; y <= t.y1; ++y)
    {
        // The texel's rows in level-0 window coordinates (the last through the image's top), within the rectangle
        const float fy0 = std::max(float(y << t.level), f.ry0);
        const float fy1 = std::min(y == l.height - 1 ? float(base.height) : float((y + 1) << t.level), f.ry1);
        if (fy0 > fy1)
            continue;
        for (int x = t.x0; x <= t.x1; ++x)
        {
            const float fx0 = std::max(float(x << t.level), f.rx0);
            const float fx1 = std::min(x == l.width - 1 ? float(base.width) : float((x + 1) << t.level), f.rx1);
            if (fx0 > fx1)
                continue;
            // Each front plane at its nearest corner of the footprint
            float bound = f.nearest;
            for (int i = 0; i < count; ++i)
            {
                const FacePlane &p = planes[i];
                const float depth = p.d0 + p.a * ((p.a > 0.0f ? fx0 : fx1) - p.x0) +
                                    p.b * ((p.b > 0.0f ? fy0 : fy1) - p.y0);
                bound = std::max(bound, depth);
            }
            if (bound <= texel(l, x, y) + OCCLUSION_DEPTH_TOLERANCE)
                return BoxVisibility::Visible;
        }
    }
    return BoxVisibility::Occluded;
}

float box_face_orientation(const float view_proj[16])
{
    return face_orientation(view_proj);
}

SegmentFootprint segment_footprint(const float view_proj[16], const float a[3], const float b[3], float radius_a,
                                   float radius_b, int width, int height)
{
    SegmentFootprint s;
    SegmentEnd ea, eb;
    if (width <= 0 || height <= 0 || !segment_end(view_proj, a, radius_a, width, height, ea) ||
        !segment_end(view_proj, b, radius_b, width, height, eb))
        return s;
    s.ax = ea.x;
    s.ay = ea.y;
    s.bx = eb.x;
    s.by = eb.y;
    s.radius_a = ea.radius;
    s.radius_b = eb.radius;
    s.footprint = footprint_of_rectangle(std::min(ea.x - ea.radius, eb.x - eb.radius),
                                         std::max(ea.x + ea.radius, eb.x + eb.radius),
                                         std::min(ea.y - ea.radius, eb.y - eb.radius),
                                         std::max(ea.y + ea.radius, eb.y + eb.radius), std::min(ea.nearest, eb.nearest),
                                         width, height);
    return s;
}

BoxVisibility test_segment_occlusion(const float view_proj[16], const float a[3], const float b[3], float radius_a,
                                     float radius_b, const DepthPyramid &pyramid, int taps)
{
    if (pyramid.levels.empty())
        return BoxVisibility::Visible;
    const DepthLevel &base = pyramid.levels.front();
    const SegmentFootprint s = segment_footprint(view_proj, a, b, radius_a, radius_b, base.width, base.height);
    const BoxFootprint &f = s.footprint;
    if (!f.bounded)
        return BoxVisibility::Visible;
    if (f.outside)
        return BoxVisibility::Outside;
    const float reach = std::max(s.radius_a, s.radius_b);
    const float dx = s.bx - s.ax;
    const float dy = s.by - s.ay;
    const float length2 = dx * dx + dy * dy;

    const PyramidTexels t = pyramid_texels(pyramid, f.x0, f.y0, f.x1, f.y1, taps);
    const DepthLevel &l = pyramid.levels[size_t(t.level)];
    for (int y = t.y0; y <= t.y1; ++y)
    {
        // The texel's rows in level-0 window coordinates (the last through the image's top), within the rectangle
        const float fy0 = std::max(float(y << t.level), f.ry0);
        const float fy1 = std::min(y == l.height - 1 ? float(base.height) : float((y + 1) << t.level), f.ry1);
        if (fy0 > fy1)
            continue;
        for (int x = t.x0; x <= t.x1; ++x)
        {
            const float fx0 = std::max(float(x << t.level), f.rx0);
            const float fx1 = std::min(x == l.width - 1 ? float(base.width) : float((x + 1) << t.level), f.rx1);
            if (fx0 > fx1)
                continue;
            // The square's distance from the window segment, at least its centre's less its half diagonal
            const float cx = 0.5f * (fx0 + fx1);
            const float cy = 0.5f * (fy0 + fy1);
            const float half_diagonal = 0.5f * std::sqrt((fx1 - fx0) * (fx1 - fx0) + (fy1 - fy0) * (fy1 - fy0));
            const float u = length2 > 0.0f ? std::clamp(((cx - s.ax) * dx + (cy - s.ay) * dy) / length2, 0.0f, 1.0f)
                                           : 0.0f;
            const float ox = cx - (s.ax + u * dx);
            const float oy = cy - (s.ay + u * dy);
            if (std::sqrt(ox * ox + oy * oy) - half_diagonal > reach)
                continue;
            if (f.nearest <= texel(l, x, y) + OCCLUSION_DEPTH_TOLERANCE)
                return BoxVisibility::Visible;
        }
    }
    return BoxVisibility::Occluded;
}

void occlusion_view_proj(const float projection[16], const float view[16], float view_proj[16])
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += projection[k * 4 + r] * view[c * 4 + k];
            view_proj[c * 4 + r] = sum;
        }
}

int depth_pyramid_levels(int width, int height)
{
    if (width <= 0 || height <= 0)
        return 0;
    int levels = 1;
    while (width > 1 || height > 1)
    {
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
        ++levels;
    }
    return levels;
}

namespace
{

// The slab order: by distance, ties by sub-cell
bool nearer(const SlabCandidate &a, const SlabCandidate &b)
{
    return a.distance < b.distance || (a.distance == b.distance && a.subcell < b.subcell);
}

} // namespace

size_t select_nearest_slab(std::vector<SlabCandidate> &candidates, size_t budget)
{
    // Every candidate before lo is in the slab and they hold `taken` segments; every candidate from hi on is farther
    // than every one before hi
    size_t lo = 0;
    size_t hi = candidates.size();
    size_t taken = 0;
    while (lo < hi)
    {
        // The median of the first, middle and last candidate of the range as the pivot, moved to the range's end
        const size_t mid = lo + (hi - lo) / 2;
        size_t a = lo;
        size_t b = mid;
        size_t c = hi - 1;
        if (nearer(candidates[b], candidates[a]))
            std::swap(a, b);
        if (nearer(candidates[c], candidates[b]))
            std::swap(b, c);
        if (nearer(candidates[b], candidates[a]))
            std::swap(a, b);
        std::swap(candidates[b], candidates[hi - 1]);
        const SlabCandidate pivot = candidates[hi - 1];
        const auto split = std::partition(candidates.begin() + std::ptrdiff_t(lo),
                                          candidates.begin() + std::ptrdiff_t(hi - 1),
                                          [&pivot](const SlabCandidate &x) { return nearer(x, pivot); });
        const size_t p = size_t(split - candidates.begin());
        std::swap(candidates[p], candidates[hi - 1]);
        // [lo, p) is nearer than the pivot, now at p, and (p, hi) farther
        size_t nearer_segments = 0;
        for (size_t i = lo; i < p; ++i)
            nearer_segments += candidates[i].segments;
        if (taken + nearer_segments > budget)
        {
            // The slab ends among the nearer ones
            hi = p;
            continue;
        }
        taken += nearer_segments;
        if (taken + candidates[p].segments > budget)
        {
            // The slab ends before the pivot
            lo = p;
            break;
        }
        taken += candidates[p].segments;
        lo = p + 1;
    }
    // The nearest candidate is at the front whenever the slab came out empty
    return candidates.empty() ? 0 : std::max<size_t>(lo, 1);
}

bool occlusion_box_bounded(const float box[6])
{
    const float limit = std::numeric_limits<float>::max();
    for (int a = 0; a < 3; ++a)
        if (!(box[a] > -limit && box[3 + a] < limit))
            return false;
    return true;
}

void order_chunks_front_to_back(const std::vector<float> &boxes, bool perspective, const float eye[3],
                                const float forward[3], std::vector<uint32_t> &chunks,
                                std::vector<ChunkDistance> &scratch)
{
    // Infinity sorts after every finite distance, and the chunk index orders those it gives
    const float last = std::numeric_limits<float>::infinity();
    scratch.resize(chunks.size());
    for (size_t i = 0; i < chunks.size(); ++i)
    {
        const uint32_t c = chunks[i];
        float distance = last;
        if (6 * size_t(c) + 6 <= boxes.size() && occlusion_box_bounded(&boxes[6 * size_t(c)]))
        {
            const float *box = &boxes[6 * size_t(c)];
            float d = 0.0f;
            for (int a = 0; a < 3; ++a)
            {
                const float centre = 0.5f * box[a] + 0.5f * box[3 + a];
                d += perspective ? (centre - eye[a]) * (centre - eye[a]) : centre * forward[a];
            }
            if (std::isfinite(d))
                distance = d;
        }
        scratch[i].distance = distance;
        scratch[i].chunk = c;
    }
    std::sort(scratch.begin(), scratch.end(), [](const ChunkDistance &a, const ChunkDistance &b)
              { return a.distance < b.distance || (a.distance == b.distance && a.chunk < b.chunk); });
    for (size_t i = 0; i < chunks.size(); ++i)
        chunks[i] = scratch[i].chunk;
}

} // namespace libvgcode
