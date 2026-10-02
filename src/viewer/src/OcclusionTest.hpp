///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace libvgcode
{

// The occlusion box test: a world box against a depth image of the frame, through a farthest-depth pyramid. Every
// step is plain float arithmetic so a vertex shader can run it line by line.
//
// Window depth follows the default depth range [0, 1]: depth = ndc.z * 0.5 + 0.5, 1 at the far plane and where nothing
// was drawn. Images are row-major with rows bottom-up, as glReadPixels returns them; texel (x, y) covers the window
// square [x, x + 1] x [y, y + 1], its centre at (x + 0.5, y + 0.5).

// A box corner whose clip w is not above this lies at or behind the eye plane, where its projection is unbounded
static constexpr float OCCLUSION_MIN_CLIP_W = 1.0e-5f;
// Window depth by which a box's nearest depth may lie behind the occluder depth and still pass: 16 steps of a 24-bit
// depth buffer, for the rounding of the stored depth and of the two projections (the GPU's of the bead, this one's of
// the box). Safe range 0 to 2^-16.
static constexpr float OCCLUSION_DEPTH_TOLERANCE = 1.0f / 1048576.0f;
// The faces test leaves out a face whose window area is not above the larger of this fraction of the box's window
// rectangle and OCCLUSION_FACE_MIN_AREA (square pixels): an edge-on face, whose orientation and plane the rounding of
// its corners decides. Leaving a face out only lowers the bound, so larger values stay sound. Safe range 1/1024 to 1/16.
static constexpr float OCCLUSION_FACE_MIN_AREA_FRACTION = 1.0f / 256.0f;
static constexpr float OCCLUSION_FACE_MIN_AREA = 1.0f / 16.0f;
// Window pixels the segment test adds to each end's projected radius: the float rounding of the shader's vertex
// positions and of this test's projection, and the rasterizer's sub-pixel snapping. A larger value only keeps more
// texels, so stays sound. Safe range 0 to 0.5.
static constexpr float OCCLUSION_SEGMENT_MARGIN = 1.0f / 16.0f;

// One level of a depth pyramid, width x height texels
struct DepthLevel
{
    int width{0};
    int height{0};
    std::vector<float> depth;
};

// Farthest-depth pyramid. Level 0 is the depth image. Level L + 1 of level L's w x h is max(1, floor(w / 2)) x
// max(1, floor(h / 2)); its texel (x, y) holds the largest of level L's texels in columns 2x and 2x + 1, plus 2x + 2
// when w is odd and x is the last column, and in rows 2y and 2y + 1, plus 2y + 2 when h is odd and y is the last row
// (a source index past level L is clamped to its last, which only a size of 1 reaches). The last level is 1 x 1.
// Each texel of level L lies under exactly one texel of level L + 1, so level-0 texel c lies under level-L texel
// min(c >> L, size_L - 1) on each axis, and every level-L texel holds the largest level-0 depth under it.
struct DepthPyramid
{
    std::vector<DepthLevel> levels;
};

// The pyramid of a width x height depth image (moved in as level 0); empty when a size is not positive or depth does
// not hold width * height values
DepthPyramid build_depth_pyramid(int width, int height, std::vector<float> depth);

// A world box's footprint on a width x height viewport, from its 8 corners projected by view_proj (column-major,
// clip = view_proj * (x, y, z, 1)):
// - unbounded when a corner's clip w is not above OCCLUSION_MIN_CLIP_W or a projected value is not finite;
// - otherwise per corner ndc = clip.xyz / clip.w, window x = (ndc.x * 0.5 + 0.5) * width, window y = (ndc.y * 0.5 +
//   0.5) * height, depth = ndc.z * 0.5 + 0.5. The rectangle is the min and max of the window x and y, nearest is the
//   min of the depths, raised to 0;
// - outside when the rectangle lies wholly off [0, width] x [0, height] (max x < 0, min x > width, max y < 0 or
//   min y > height) or nearest is above 1;
// - otherwise the rectangle clamped to the viewport gives the level-0 texel range x0 = floor(min x), x1 = ceil(max x)
//   - 1, each clamped to [0, width - 1], then x1 raised to x0 (a rectangle of zero extent covers one texel); the same
//   for y with height. The range holds every texel whose centre the rectangle holds.
struct BoxFootprint
{
    bool bounded{false};
    bool outside{false};
    int x0{0}, y0{0}, x1{0}, y1{0};                   // inclusive level-0 texel range (bounded and not outside only)
    float nearest{0.0f};                              // nearest window depth, at least 0 (bounded only)
    float rx0{0.0f}, ry0{0.0f}, rx1{0.0f}, ry1{0.0f}; // the rectangle clamped to the viewport (as the range)
};
BoxFootprint box_footprint(const float view_proj[16], const float box_min[3], const float box_max[3], int width,
                           int height);

// The pyramid level the lookup reads for a level-0 texel range [x0, x1] x [y0, y1] (clamped to level 0 first), and
// the range's texels there: indices at level L are min(index >> L, size_L - 1); L is the smallest level at which
// x1_L - x0_L <= taps - 1 and y1_L - y0_L <= taps - 1 (taps at least 1), so at most taps x taps texels are read; 2 for
// the box test. Level 0 and the range unchanged for an empty pyramid.
struct PyramidTexels
{
    int level{0};
    int x0{0}, y0{0}, x1{0}, y1{0};
};
PyramidTexels pyramid_texels(const DepthPyramid &pyramid, int x0, int y0, int x1, int y1, int taps = 2);

// The occluder depth of a level-0 texel range: the largest of the (at most 4) texels pyramid_texels names. Never
// smaller than exact_farthest over the same range, since every level-0 texel of the range lies under one of them.
// 1 for an empty pyramid.
float pyramid_farthest(const DepthPyramid &pyramid, int x0, int y0, int x1, int y1);
// The largest level-0 depth over a level-0 texel range (clamped to level 0); 1 for an empty pyramid
float exact_farthest(const DepthPyramid &pyramid, int x0, int y0, int x1, int y1);

enum class BoxVisibility : uint8_t
{
    Outside,  // off the viewport, or wholly beyond the far plane
    Occluded, // behind the occluder depth of its whole rectangle
    Visible,  // passes; also any box whose footprint is unbounded
};

// The box test on the pyramid's level 0 viewport: Visible for an unbounded footprint or an empty pyramid, Outside for
// an outside one, else Visible when nearest <= pyramid_farthest(range) + OCCLUSION_DEPTH_TOLERANCE, Occluded
// otherwise. Conservative: a box that holds a bead which is the nearest surface at a texel centre of its rectangle
// passes, since that texel's depth is no nearer than the bead and the bead no nearer than nearest.
BoxVisibility test_box_occlusion(const float view_proj[16], const float box_min[3], const float box_max[3],
                                 const DepthPyramid &pyramid);
// The same test with exact_farthest in place of pyramid_farthest, for comparison. Since pyramid_farthest is never
// smaller, test_box_occlusion is never Occluded where this is Visible.
BoxVisibility test_box_occlusion_exact(const float view_proj[16], const float box_min[3], const float box_max[3],
                                       const DepthPyramid &pyramid);

// The faces of a box (box_min <= box_max on every axis): face 2a is the box_min side of axis a, face 2a + 1 the box_max
// side. Corner i takes the max on axis a when bit a of i is set. Each face's corners, counter-clockwise seen from outside
// the box:
static constexpr int OCCLUSION_BOX_FACES[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4},
                                                  {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};

// The front faces of a box on a width x height viewport, bit k for face k: the faces whose outer side holds the eye.
// Projected to window x and y, a face's corners in the order above enclose a signed area 0.5 * (cross(p1 - p0,
// p2 - p0) + cross(p2 - p0, p3 - p0)), cross(u, v) = u.x * v.y - u.y * v.x. With view_proj's determinant negative, as
// for every standard view and projection (both orthographic and perspective projections flip z), a front face has a
// positive area; with a positive determinant, a negative one. A face is front when its area, times -1 for a positive
// determinant, is above the edge-on limit (OCCLUSION_FACE_MIN_AREA_FRACTION of the box's window rectangle, at least
// OCCLUSION_FACE_MIN_AREA). At most three faces, one per axis. 0 for an unbounded footprint (see box_footprint), a box
// with box_min above box_max on an axis, a zero or non-finite determinant, or a viewport of no size.
uint8_t box_front_faces(const float view_proj[16], const float box_min[3], const float box_max[3], int width,
                        int height);

// The faces test: the box test with the depth of the box's front faces in place of its one nearest depth.
// - Visible for an unbounded footprint or an empty pyramid, Outside for an outside one (both as box_footprint), and
//   Visible when a corner's depth is below 0 (nearer than the near plane: the box test passes such a box too).
// - Each front face (box_front_faces) gives its plane in window space, d = d0 + A (x - x0) + B (y - y0) through its
//   corner p0 = (x0, y0, d0), fitted to the larger of its triangles p0 p1 p2 and p0 p2 p3: with u = pa - p0,
//   v = pb - p0 and D = cross(u, v), A = (u.d * v.y - u.y * v.d) / D and B = (u.x * v.d - u.d * v.x) / D. Window depth
//   is affine in window x and y on a plane, in perspective too.
// - The level is pyramid_texels(range, taps) with taps texels per axis (4 or 8). A texel (x, y) of level L with size
//   W_L x H_L covers the level-0 window square [x << L, x == W_L - 1 ? width : (x + 1) << L] by the same in y; that,
//   intersected with the clamped rectangle (rx0..rx1, ry0..ry1), is its footprint [fx0, fx1] x [fy0, fy1] (a texel
//   whose intersection is empty is skipped).
// - Per texel, bound = max(nearest, over the front faces d0 + A ((A > 0 ? fx0 : fx1) - x0) + B ((B > 0 ? fy0 : fy1) -
//   y0)), each face's plane at its nearest corner of the footprint. The box is Visible when bound <= texel depth +
//   OCCLUSION_DEPTH_TOLERANCE for some texel, Occluded otherwise.
// Why a box that holds a bead which is the nearest surface at a pixel centre is never Occluded: the centre lies in the
// box's rectangle and in the footprint of the texel above it at level L. The ray through the centre enters the convex
// box through the front plane it reaches last, so its entry depth is the largest of the front planes' depths at the
// centre, and the bead lies no nearer than that entry. Each plane's depth at the centre is at least its minimum over
// the footprint and the entry is at least nearest, so bound <= entry <= bead depth. The texel holds the farthest depth
// under it, which is no nearer than the bead's at that pixel. Hence bound <= texel depth. The two conservative steps
// are the plane minimum over the whole footprint in place of the plane at the centre, and the pyramid maximum over the
// texel in place of the pixel's own depth; a front face left out only lowers the bound.
// With no front face the bound is nearest alone. With taps >= 2, never Visible where test_box_occlusion is not: every
// texel read here lies under one of its 2 x 2 texels, and bound >= nearest. Nor where the same test with fewer taps is
// not: each texel's ancestor at the coarser level covers its footprint, so its bound is no larger and its depth no
// smaller.
BoxVisibility test_box_occlusion_faces(const float view_proj[16], const float box_min[3], const float box_max[3],
                                       const DepthPyramid &pyramid, int taps);

// The sign box_front_faces gives a front face's window area for view_proj: 1 for a negative determinant, -1 for a
// positive one, 0 for a zero or non-finite determinant
float box_face_orientation(const float view_proj[16]);

// A segment's footprint on a width x height viewport. Its bead lies in the convex hull of two balls, radius_a around a
// and radius_b around b (radii taken by magnitude). Clip row r of a point p is g_r . p + view_proj[12 + r] with
// g_r = (view_proj[r], view_proj[4 + r], view_proj[8 + r]). Per end p of radius r:
// 1. c = view_proj (p, 1) and wmin = c.w - r |g_3|, the least clip w over the ball. Unbounded when wmin is not above
//    OCCLUSION_MIN_CLIP_W (the ball reaches the eye plane) or a value below is not finite.
// 2. ndc = c.xyz / c.w; the centre's window x = (ndc.x * 0.5 + 0.5) * width, y = (ndc.y * 0.5 + 0.5) * height and
//    depth = ndc.z * 0.5 + 0.5.
// 3. e_x = 0.5 width (g_0 - ndc.x g_3), e_y = 0.5 height (g_1 - ndc.y g_3), e_z = 0.5 (g_2 - ndc.z g_3). With
//    p2 = e_x . e_x, q2 = e_y . e_y and s = e_x . e_y, lambda = 0.5 (p2 + q2) + sqrt(0.25 (p2 - q2)^2 + s^2) is the
//    largest eigenvalue of [p2 s; s q2]. The end's window radius is r sqrt(lambda) / wmin + OCCLUSION_SEGMENT_MARGIN
//    and its nearest depth is depth - r |e_z| / wmin.
// The footprint's nearest is the smaller of the ends' nearest depths, and its rectangle the min and max over the ends of
// x -+ radius and y -+ radius; from them it takes the raise to 0, the outside rule, the clamped rectangle and the texel
// range exactly as box_footprint does from its corners.
// Why no point of the hull lies outside the rectangle, farther than the larger radius from the window segment between
// the ends' centres, or nearer than nearest: for q = p + v with |v| <= r each clip row moves by g_r . v, so exactly
// x(q) - x(p) = (e_x . v) / c_w(q), the same with e_y for window y and e_z for depth, and c_w(q) >= wmin > 0. The
// window offset is then at most r sqrt(lambda) / wmin long (sqrt(lambda) is the largest singular value of the 3 x 2
// matrix [e_x e_y]) and depth(q) >= depth - r |e_z| / wmin, for any projection and off the view axis too; for an
// orthographic one (g_3 = 0, c.w = 1) the radius is the ball's exact outline, and for a standard perspective one the
// depth bound is the depth of the ball's point nearest the eye. Clip w is affine, so its least value over the hull is
// a ball's: the hull lies in front of the eye plane, where the projection maps segments to segments. So the hull's window
// image lies in the convex hull of the two discs, whose point k X + (1 - k) Y (X, Y in the discs) lies within
// k R_a + (1 - k) R_b of k A + (1 - k) B on the segment. And {q : c_w(q) > 0, depth(q) >= t}, the intersection of the
// half-spaces c_w > 0 and c_z - (2t - 1) c_w >= 0, is convex: with t the smaller nearest depth it holds both balls, so
// the hull.
struct SegmentFootprint
{
    BoxFootprint footprint;               // bounded, outside, texel range, nearest and clamped rectangle
    float ax{0.0f}, ay{0.0f};             // window point of a's centre (bounded only)
    float bx{0.0f}, by{0.0f};             // window point of b's centre
    float radius_a{0.0f}, radius_b{0.0f}; // window radius of each end's ball, the margin included
};
SegmentFootprint segment_footprint(const float view_proj[16], const float a[3], const float b[3], float radius_a,
                                   float radius_b, int width, int height);

// The segment test on the pyramid's level 0 viewport, the footprint from segment_footprint:
// - Visible for an unbounded footprint or an empty pyramid, Outside for an outside one;
// - the level and texels are pyramid_texels(range, taps), taps texels per axis; a texel's square [fx0, fx1] x [fy0, fy1]
//   is the faces test's footprint (the texel's level-0 window square within the clamped rectangle; an empty one is
//   skipped);
// - with A and B the ends' window points, d = B - A, the square's centre c and half diagonal h = 0.5 sqrt((fx1 - fx0)^2
//   + (fy1 - fy0)^2), u = clamp(dot(c - A, d) / dot(d, d), 0, 1) (0 when dot(d, d) is 0): the texel is skipped when
//   |c - (A + u d)| - h > max(radius_a, radius_b), since every point of the square lies at least that far from the
//   segment A B;
// - Visible when nearest <= texel depth + OCCLUSION_DEPTH_TOLERANCE at a kept texel, Occluded otherwise.
// Why a bead inside the balls' hull that is the nearest surface at a pixel centre is never Occluded: the centre lies in
// the hull's window image, so in the clamped rectangle and within the larger radius of the segment, and in the square
// of the texel above it at the level, which is therefore kept. The bead's depth there is at least nearest, and the
// texel holds the farthest depth under it, at least that pixel's, which is the bead's. Hence nearest <= texel depth.
// For the segments shader the caller passes per end its position as the shader reads it and
// max(half height, half width): every vertex the shader places for an end lies within that distance of it (the joint
// and cap vertices at half the width, in either branch of its line frame), and the triangles in the hull of their
// vertices. The clipping plane's cap vertices are not covered: they slide along the line onto the plane.
BoxVisibility test_segment_occlusion(const float view_proj[16], const float a[3], const float b[3], float radius_a,
                                     float radius_b, const DepthPyramid &pyramid, int taps);

// Column-major projection * view in float, each entry summed over k = 0 to 3 in order, as the chunk selection forms it
void occlusion_view_proj(const float projection[16], const float view[16], float view_proj[16]);

// The level count build_depth_pyramid makes for a width x height image; 0 for a size that is not positive
int depth_pyramid_levels(int width, int height);

// A candidate of the occlusion refinement's slab: a sub-cell, its distance from the camera (smaller is nearer, never
// NaN) and the segments it holds
struct SlabCandidate
{
    float distance{0.0f};
    uint32_t subcell{0};
    uint32_t segments{0};
};

// The slab: the candidates nearest first in the order of (distance, subcell), as many as hold at most `budget` segments
// together, and at least the nearest one (none for no candidate). Moves them to the front of `candidates` and returns
// how many; the order within the slab and after it is unspecified. The slab does not depend on the input order; a
// weighted selection in the manner of std::nth_element finds it in expected linear time.
size_t select_nearest_slab(std::vector<SlabCandidate> &candidates, size_t budget);

// Whether a box (6 floats, min x, y, z then max x, y, z) bounds anything: false when an axis reaches the largest float
// or holds a NaN, as the chunk structure marks a box it cannot bound
bool occlusion_box_bounded(const float box[6]);

// A chunk of the draw order and its distance from the camera (smaller is nearer)
struct ChunkDistance
{
    float distance{0.0f};
    uint32_t chunk{0};
};

// Puts the chunks `chunks` lists (indices of `boxes`, 6 floats per chunk as occlusion_box_bounded reads them) in draw
// order, nearest first, in place. A chunk's distance is that of its box's centre (0.5 min + 0.5 max per axis): with
// `perspective` its squared distance from `eye`, otherwise its depth along `forward` (the view direction), the dot
// product of the centre with it. Equal distances go by chunk index. A chunk whose box is not bounded, whose distance is
// not finite or whose index is past the boxes goes after every other one, by chunk index. `scratch` is reused: nothing
// is allocated once it holds as many entries as `chunks`.
void order_chunks_front_to_back(const std::vector<float> &boxes, bool perspective, const float eye[3],
                                const float forward[3], std::vector<uint32_t> &chunks,
                                std::vector<ChunkDistance> &scratch);

} // namespace libvgcode
