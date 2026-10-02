///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "VaseWall.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Luminary::VaseWall
{
namespace
{
using Athena::ExtrusionJunction;
using Athena::ExtrusionLine;
using Junctions = std::vector<ExtrusionJunction>;

// Vector math runs in double on the scaled coordinates; results are rounded when they become
// junctions.
Vec2d to_vec(const Point &p)
{
    return Vec2d(double(p.x()), double(p.y()));
}

Vec2d unit(const Vec2d &v)
{
    const double n = v.norm();
    return n > 0. ? Vec2d(v.x() / n, v.y() / n) : Vec2d(0., 0.);
}

double cross(const Vec2d &a, const Vec2d &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

Vec2d left_normal(const Vec2d &d)
{
    return Vec2d(-d.y(), d.x());
}

coord_t round_coord(double v)
{
    return coord_t(std::llround(v));
}

coord_t half_width(coord_t w)
{
    return round_coord(0.5 * double(w));
}

double polyline_length(const Junctions &pts)
{
    double length = 0.;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        length += (to_vec(pts[i + 1].p) - to_vec(pts[i].p)).norm();
    return length;
}

// A point on the loop: the segment it lies on (from junction `segment` to `segment + 1`), its
// parameter along that segment and its distance from the query point.
struct LoopPoint
{
    size_t segment = 0;
    double t = 0.;
    double distance = 0.;
    Vec2d point = Vec2d(0., 0.);
};

// Nearest point of the closed junction list to p; on a tie the earlier segment wins.
LoopPoint nearest_on_loop(const Junctions &loop, const Point &p)
{
    const Vec2d q = to_vec(p);
    LoopPoint best;
    bool found = false;
    for (size_t s = 0; s + 1 < loop.size(); ++s)
    {
        const Vec2d a = to_vec(loop[s].p);
        const Vec2d ab = to_vec(loop[s + 1].p) - a;
        const double l2 = ab.squaredNorm();
        const double t = l2 == 0. ? 0. : std::clamp((q - a).dot(ab) / l2, 0., 1.);
        const Vec2d on = a + ab * t;
        const double d = (on - q).norm();
        if (!found || d < best.distance)
        {
            best = LoopPoint{s, t, d, on};
            found = true;
        }
    }
    return best;
}

// Left unit normal per junction: the adjacent segment's at the two ends, the normalized average of
// the two adjacent segment directions inside. At a hairpin, where that average vanishes, the
// incoming direction is kept.
std::vector<Vec2d> junction_normals(const Junctions &pts)
{
    const size_t n = pts.size();
    std::vector<Vec2d> seg;
    seg.reserve(n - 1);
    for (size_t i = 0; i + 1 < n; ++i)
        seg.emplace_back(unit(to_vec(pts[i + 1].p) - to_vec(pts[i].p)));
    std::vector<Vec2d> normals;
    normals.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        Vec2d d;
        if (i == 0)
            d = seg.front();
        else if (i == n - 1)
            d = seg.back();
        else
        {
            d = unit(seg[i - 1] + seg[i]);
            if (d.norm() == 0.)
                d = seg[i - 1];
        }
        normals.emplace_back(left_normal(d));
    }
    return normals;
}

// The fin's junctions moved `sign * w / 4` along their normals, each at half its width but never
// under the floor. An out pass and a back pass of opposite signs together lay down the fin's own
// volume.
Junctions offset_pass(const Junctions &fin, const std::vector<Vec2d> &normals, double sign, size_t perimeter_index,
                      coord_t min_pass_width)
{
    Junctions pass;
    pass.reserve(fin.size());
    for (size_t i = 0; i < fin.size(); ++i)
    {
        const Vec2d q = to_vec(fin[i].p) + normals[i] * (sign * double(fin[i].w) / 4.);
        pass.emplace_back(Point(q.x(), q.y()), std::max(half_width(fin[i].w), min_pass_width),
                          coord_t(perimeter_index));
    }
    return pass;
}

// The excursion for `fin` (attachment end first) at the loop point `at` on the loop segment a-b:
// the connector from the loop point, the out pass, the hop across the tip, the back pass and the
// connector to the same loop point. The out pass runs on the side the loop arrives from (left of
// the fin when the fin turns left off the reversed incoming direction), so the loop enters and
// leaves the excursion without crossing it.
Junctions excursion_for(const ExtrusionJunction &a, const ExtrusionJunction &b, const LoopPoint &at,
                        const Junctions &fin, coord_t min_pass_width)
{
    const Vec2d d_in = unit(to_vec(b.p) - to_vec(a.p));
    const Vec2d d_fin = unit(to_vec(fin[1].p) - to_vec(fin[0].p));
    const Vec2d d_in_reversed = -d_in;
    const double side = cross(d_fin, d_in_reversed) >= 0. ? 1. : -1.;
    const coord_t w_loop = round_coord(double(a.w) + double(b.w - a.w) * at.t);
    const std::vector<Vec2d> normals = junction_normals(fin);
    const Junctions out = offset_pass(fin, normals, side, a.perimeter_index, min_pass_width);
    const Junctions back = offset_pass(fin, normals, -side, a.perimeter_index, min_pass_width);
    const Point p(at.point.x(), at.point.y());

    Junctions excursion;
    excursion.reserve(2 * fin.size() + 2);
    excursion.emplace_back(p, w_loop, coord_t(a.perimeter_index));
    excursion.insert(excursion.end(), out.begin(), out.end());
    excursion.insert(excursion.end(), back.rbegin(), back.rend());
    excursion.emplace_back(p, w_loop, coord_t(a.perimeter_index));
    return excursion;
}

// Twice the signed area of the triangle o-a-b, exact in 64 bits for any island narrower than 2^31
// scaled units (2 m).
int64_t twice_signed_area(const Point &o, const Point &a, const Point &b)
{
    const int64_t ax = a.x() - o.x(), ay = a.y() - o.y();
    const int64_t bx = b.x() - o.x(), by = b.y() - o.y();
    return ax * by - ay * bx;
}

// The segments p1-p2 and p3-p4 cross at an interior point of both. Touching (a shared endpoint or
// an endpoint on the other segment) and collinear overlap do not count.
bool proper_cross(const Point &p1, const Point &p2, const Point &p3, const Point &p4)
{
    const int64_t d1 = twice_signed_area(p3, p4, p1);
    const int64_t d2 = twice_signed_area(p3, p4, p2);
    const int64_t d3 = twice_signed_area(p1, p2, p3);
    const int64_t d4 = twice_signed_area(p1, p2, p4);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

// Any proper crossing of the excursion's segments with the loop (segment `s`, where the excursion
// attaches, excluded) or with the excursion's own non-adjacent segments. The loop already carries
// the excursions spliced before this one, so they are tested too.
bool excursion_crosses(const Junctions &excursion, const Junctions &loop, size_t s)
{
    for (size_t i = 0; i + 1 < excursion.size(); ++i)
    {
        const Point &e0 = excursion[i].p;
        const Point &e1 = excursion[i + 1].p;
        for (size_t j = 0; j + 1 < loop.size(); ++j)
            if (j != s && proper_cross(e0, e1, loop[j].p, loop[j + 1].p))
                return true;
        for (size_t j = i + 2; j + 1 < excursion.size(); ++j)
            if (proper_cross(e0, e1, excursion[j].p, excursion[j + 1].p))
                return true;
    }
    return false;
}

// A lone fin as a closed loop: the out pass on its left, the back pass on its right, closed at the
// start of the out pass.
Junctions slot_loop(const Junctions &fin, coord_t min_pass_width)
{
    const std::vector<Vec2d> normals = junction_normals(fin);
    const size_t perimeter_index = fin.front().perimeter_index;
    Junctions loop = offset_pass(fin, normals, 1., perimeter_index, min_pass_width);
    const Junctions back = offset_pass(fin, normals, -1., perimeter_index, min_pass_width);
    loop.insert(loop.end(), back.rbegin(), back.rend());
    const ExtrusionJunction first = loop.front();
    loop.push_back(first);
    return loop;
}

FinReport fin_report(FinReport::Outcome outcome, const ExtrusionLine &fin, double gap)
{
    FinReport report;
    report.outcome = outcome;
    report.length = round_coord(polyline_length(fin.junctions));
    report.min_width = fin.junctions.front().w;
    report.max_width = fin.junctions.front().w;
    for (const ExtrusionJunction &j : fin.junctions)
    {
        report.min_width = std::min(report.min_width, j.w);
        report.max_width = std::max(report.max_width, j.w);
    }
    report.gap = round_coord(gap);
    report.odd = fin.is_odd;
    return report;
}

struct LineRef
{
    size_t inset;
    size_t line;
};

// A fin placed against the loop: where it attaches, the loop segment's end junctions as they were
// before any splice, and its junctions with the attachment end first.
struct Attachment
{
    LoopPoint at;
    ExtrusionJunction a;
    ExtrusionJunction b;
    Junctions fin;
    const ExtrusionLine *line;
};

} // namespace

Report make_single_loop(Athena::Perimeters &perimeters, coord_t attach_tolerance, coord_t min_pass_width)
{
    Report report;

    // The closed lines are loops; every open line with at least one segment is a fin.
    std::vector<LineRef> loops;
    std::vector<LineRef> fins;
    for (size_t i = 0; i < perimeters.size(); ++i)
        for (size_t j = 0; j < perimeters[i].size(); ++j)
        {
            const ExtrusionLine &el = perimeters[i][j];
            if (el.is_closed)
            {
                if (!el.junctions.empty())
                    loops.push_back({i, j});
            }
            else if (el.junctions.size() >= 2)
                fins.push_back({i, j});
        }
    report.loops = loops.size();
    if (loops.size() >= 2 || fins.empty())
        return report;

    auto line_at = [&perimeters](const LineRef &ref) -> ExtrusionLine &
    {
        return perimeters[ref.inset][ref.line];
    };

    // Without a loop the longest fin becomes a closed slot loop; the other fins attach to it.
    Junctions loop;
    LineRef target{};
    if (loops.empty())
    {
        size_t longest = 0;
        double longest_length = -1.;
        for (size_t k = 0; k < fins.size(); ++k)
        {
            const double length = polyline_length(line_at(fins[k]).junctions);
            if (length > longest_length)
            {
                longest = k;
                longest_length = length;
            }
        }
        target = fins[longest];
        const ExtrusionLine &fin = line_at(target);
        loop = slot_loop(fin.junctions, min_pass_width);
        report.fins.push_back(fin_report(FinReport::Outcome::Slot, fin, 0.));
        fins.erase(fins.begin() + std::ptrdiff_t(longest));
    }
    else
    {
        target = loops.front();
        loop = line_at(target).junctions;
        // A closed line without a ring of segments is left as it is, fins included.
        if (loop.size() < 3)
        {
            report.degenerate_loop = true;
            return report;
        }
        if (loop.front().p != loop.back().p)
        {
            const ExtrusionJunction first = loop.front();
            loop.push_back(first);
        }
    }

    // Each fin attaches at its end nearer the loop; a fin whose nearer end is beyond the tolerance
    // is dropped.
    std::vector<Attachment> attachments;
    for (const LineRef &ref : fins)
    {
        const ExtrusionLine &fin = line_at(ref);
        Junctions junctions = fin.junctions;
        const LoopPoint at_front = nearest_on_loop(loop, junctions.front().p);
        const LoopPoint at_back = nearest_on_loop(loop, junctions.back().p);
        LoopPoint at = at_front;
        if (at_back.distance < at_front.distance)
        {
            std::reverse(junctions.begin(), junctions.end());
            at = at_back;
        }
        if (at.distance > double(attach_tolerance))
        {
            report.fins.push_back(fin_report(FinReport::Outcome::DroppedFar, fin, at.distance));
            continue;
        }
        attachments.push_back(Attachment{at, loop[at.segment], loop[at.segment + 1], std::move(junctions), &fin});
    }

    // Splice the later loop positions first so the earlier segment indices stay valid; fins at the
    // same position keep their input order. A fin whose excursion would cross the loop, or an
    // excursion already spliced into it, is dropped instead.
    std::stable_sort(attachments.begin(), attachments.end(), [](const Attachment &l, const Attachment &r)
                     { return l.at.segment != r.at.segment ? l.at.segment > r.at.segment : l.at.t > r.at.t; });
    for (const Attachment &att : attachments)
    {
        const Junctions excursion = excursion_for(att.a, att.b, att.at, att.fin, min_pass_width);
        if (excursion_crosses(excursion, loop, att.at.segment))
        {
            report.fins.push_back(fin_report(FinReport::Outcome::DroppedCrossing, *att.line, att.at.distance));
            continue;
        }
        loop.insert(loop.begin() + std::ptrdiff_t(att.at.segment + 1), excursion.begin(), excursion.end());
        report.fins.push_back(fin_report(FinReport::Outcome::Spliced, *att.line, att.at.distance));
    }

    // The loop keeps its line (inset and source polygon); every open line goes, and so does any
    // inset left empty.
    ExtrusionLine &out = line_at(target);
    out.junctions = std::move(loop);
    out.is_closed = true;
    out.is_odd = false;
    for (Athena::Perimeter &inset : perimeters)
        inset.erase(std::remove_if(inset.begin(), inset.end(), [](const ExtrusionLine &el) { return !el.is_closed; }),
                    inset.end());
    perimeters.erase(std::remove_if(perimeters.begin(), perimeters.end(),
                                    [](const Athena::Perimeter &inset) { return inset.empty(); }),
                     perimeters.end());
    report.changed = true;
    return report;
}

} // namespace Luminary::VaseWall
