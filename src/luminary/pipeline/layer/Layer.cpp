///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/ Copyright (c) Slic3r 2014 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ Copyright (c) Prusa Research 2016 - 2022 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "luminary/layer/model/Layer.hpp"

#include <boost/log/trivial.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <numeric>
#include <tuple>
#include <cassert>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "luminary/geometry/clipper/ClipperZUtils.hpp"
#include "luminary/geometry/clipper/ClipperUtils.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/layer/print/Print.hpp"
#include "luminary/toolpath/ordering/ShortestPath.hpp"
#include "luminary/geometry/diagnostics/SVG.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "luminary/toolpath/extrusion/ExtrusionEntity.hpp"
#include "luminary/toolpath/extrusion/ExtrusionEntityCollection.hpp"
#include "luminary/layer/region/LayerRegion.hpp"
#include "luminary/walls/perimeter/PerimeterGenerator.hpp"

#include "luminary/config/catalog/PrintConfig.hpp"
#include "luminary/geometry/surface/Surface.hpp"
#include "luminary/geometry/surface/SurfaceCollection.hpp"
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/core/Prelude.hpp"
#include "luminary/core/diagnostics/DebugOutput.hpp"
#include "luminary/toolpath/diagnostics/DebugFingerprint.hpp"

namespace Luminary
{

Layer::~Layer()
{
    this->lower_layer = this->upper_layer = nullptr;
    for (LayerRegion *region : m_regions)
        delete region;
    m_regions.clear();
}

// Test whether whether there are any slices assigned to this layer.
bool Layer::empty() const
{
    for (const LayerRegion *layerm : m_regions)
        if (layerm != nullptr && !layerm->slices().empty())
            // Non empty layer.
            return false;
    return true;
}

LayerRegion *Layer::add_region(const PrintRegion *print_region)
{
    m_regions.emplace_back(new LayerRegion(this, print_region));
    return m_regions.back();
}

// merge all regions' slices to get islands
void Layer::make_slices()
{
    {
        ExPolygons slices;
        if (m_regions.size() == 1)
        {
            // optimization: if we only have one region, take its slices
            slices = to_expolygons(m_regions.front()->slices().surfaces);
        }
        else
        {
            Polygons slices_p;
            for (LayerRegion *layerm : m_regions)
                polygons_append(slices_p, to_polygons(layerm->slices().surfaces));
            slices = union_safety_offset_ex(slices_p);
        }
        // lslices are sorted by topological order from outside to inside from the clipper union used above
        this->lslices = slices;
    }

    this->lslice_indices_sorted_by_print_order = chain_expolygons(this->lslices);
}

// used by Layer::build_up_down_graph()
// Shrink source polygons one by one, so that they will be separated if they were touching
// at vertices (non-manifold situation).
// Then convert them to Z-paths with Z coordinate indicating index of the source expolygon.
[[nodiscard]] static ClipperZUtils::ZPaths expolygons_to_zpaths_shrunk(const ExPolygons &expolygons, coord_t isrc)
{
    size_t num_paths = 0;
    for (const ExPolygon &expolygon : expolygons)
        num_paths += expolygon.num_contours();

    ClipperZUtils::ZPaths out;
    out.reserve(num_paths);

    Clipper2Lib::Paths64 contours;
    Clipper2Lib::Paths64 holes;
    Clipper2Lib::Clipper64 clipper;
    Clipper2Lib::ClipperOffset co;
    Clipper2Lib::Paths64 out2;

    // Top / bottom surfaces must overlap more than 2um to be chained into a Z graph.
    // Also a larger offset will likely be more robust on non-manifold input polygons.
    static constexpr const float delta = scaled<float>(0.001);
    // Don't scale the miter limit, it is a factor, not an absolute length!
    co.MiterLimit(3.);
    // Use the default zero edge merging distance. For this kind of safety offset the accuracy of normal direction is not important.
    //    co.ShortestEdgeLength = delta * ClipperOffsetShortestEdgeFactor;

    for (const ExPolygon &expoly : expolygons)
    {
        contours.clear();
        co.Clear();
        co.AddPath(Points_to_ClipperPath(expoly.contour.points), JoinType::Miter, EndType::Polygon);
        co.Execute(-delta, contours); // Clipper2: delta first, result second
        if (!contours.empty())
        {
            holes.clear();
            for (const Polygon &hole : expoly.holes)
            {
                co.Clear();
                co.AddPath(Points_to_ClipperPath(hole.points), JoinType::Miter, EndType::Polygon);
                // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
                // contours will be CCW oriented even though the input paths are CW oriented.
                // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
                out2.clear();
                co.Execute(delta, out2); // Clipper2: delta first, result second
                append(holes, std::move(out2));
            }
            // Subtract holes from the contours.
            if (!holes.empty())
            {
                clipper.Clear();
                clipper.AddSubject(contours);
                clipper.AddClip(holes);
                contours.clear();
                clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, contours);
            }
            for (const auto &contour : contours)
            {
                bool accept = true;
                if (accept)
                {
                    out.emplace_back();
                    ClipperZUtils::ZPath &path = out.back();
                    path.reserve(contour.size());
                    for (const Clipper2Lib::Point64 &p : contour)
                        path.push_back({p.x, p.y, isrc});
                }
            }
        }
        ++isrc;
    }

    return out;
}

// This function reads Z values directly from polytree points to identify layer slice intersections.
// Re-enabled now that USINGZ is enabled in Clipper2.
// used by Layer::build_up_down_graph()
static void connect_layer_slices(Layer &below, Layer &above, const Clipper2Lib::PolyTree64 &polytree,
                                 const std::vector<std::pair<coord_t, coord_t>> &intersections,
                                 const coord_t offset_below, const coord_t offset_above
#ifndef NDEBUG
                                 ,
                                 const coord_t offset_end
#endif // NDEBUG
)
{
    class Visitor
    {
    public:
        Visitor(const std::vector<std::pair<coord_t, coord_t>> &intersections, Layer &below, Layer &above,
                const coord_t offset_below, const coord_t offset_above
#ifndef NDEBUG
                ,
                const coord_t offset_end
#endif // NDEBUG
                )
            : m_intersections(intersections)
            , m_below(below)
            , m_above(above)
            , m_offset_below(offset_below)
            , m_offset_above(offset_above)
#ifndef NDEBUG
            , m_offset_end(offset_end)
#endif // NDEBUG
        {
        }

        void visit(const Clipper2Lib::PolyPath64 &polynode)
        {
#ifndef NDEBUG
            auto assert_intersection_valid = [this](int i, int j)
            {
                assert(i < j);
                assert(i >= m_offset_below);
                assert(i < m_offset_above);
                assert(j >= m_offset_above);
                assert(j < m_offset_end);
                return true;
            };
#endif // NDEBUG
            if (polynode.Polygon().size() >= 3)
            {
                // If there is an intersection point, it should indicate which contours (one from layer below, the other from layer above) intersect.
                // Otherwise the contour is fully inside another contour.
                auto [i, j] = this->find_top_bottom_contour_ids_strict(polynode);
                bool found = false;
                if (i < 0 && j < 0)
                {
                    // This should not happen. It may only happen if the source contours had just self intersections or intersections with contours at the same layer.
                    // We may safely ignore such cases where the intersection area is meager.
                    double a = Clipper2Lib::Area(polynode.Polygon());
                    if (a < sqr(scaled<double>(0.001)))
                    {
                        // Ignore tiny overlaps. They are not worth resolving.
                    }
                    else
                    {
                        // We should not ignore large cases. Try to resolve the conflict by a majority of references.
                        std::tie(i, j) = this->find_top_bottom_contour_ids_approx(polynode);
                        // At least top or bottom should be resolved.
                        assert(i >= 0 || j >= 0);
                    }
                }
                if (j < 0)
                {
                    if (i < 0)
                    {
                        // this->find_top_bottom_contour_ids_approx() shoudl have made sure this does not happen.
                        assert(false);
                    }
                    else
                    {
                        assert(i >= m_offset_below && i < m_offset_above);
                        i -= m_offset_below;
                        j = this->find_other_contour_costly(polynode, m_above, j == -2);
                        found = j >= 0;
                    }
                }
                else if (i < 0)
                {
                    assert(j >= m_offset_above && j < m_offset_end);
                    j -= m_offset_above;
                    i = this->find_other_contour_costly(polynode, m_below, i == -2);
                    found = i >= 0;
                }
                else
                {
                    assert(assert_intersection_valid(i, j));
                    i -= m_offset_below;
                    j -= m_offset_above;
                    assert(i >= 0 && i < m_below.lslices_ex.size());
                    assert(j >= 0 && j < m_above.lslices_ex.size());
                    found = true;
                }
                if (found)
                {
                    assert(i >= 0 && i < m_below.lslices_ex.size());
                    assert(j >= 0 && j < m_above.lslices_ex.size());
                    // Subtract area of holes from the area of outer contour.
                    double area = Clipper2Lib::Area(polynode.Polygon());
                    for (int icontour = 0; icontour < polynode.Count(); ++icontour)
                        area -= Clipper2Lib::Area(polynode[icontour]->Polygon());
                    // Store the links and area into the contours.
                    LayerSlice::Links &links_below = m_below.lslices_ex[i].overlaps_above;
                    LayerSlice::Links &links_above = m_above.lslices_ex[j].overlaps_below;
                    LayerSlice::Link key{j};
                    auto it_below = std::lower_bound(links_below.begin(), links_below.end(), key,
                                                     [](auto &l, auto &r) { return l.slice_idx < r.slice_idx; });
                    if (it_below != links_below.end() && it_below->slice_idx == j)
                    {
                        it_below->area += area;
                    }
                    else
                    {
                        auto it_above = std::lower_bound(links_above.begin(), links_above.end(), key,
                                                         [](auto &l, auto &r) { return l.slice_idx < r.slice_idx; });
                        if (it_above != links_above.end() && it_above->slice_idx == i)
                        {
                            it_above->area += area;
                        }
                        else
                        {
                            // Insert into one of the two vectors.
                            bool take_below = false;
                            if (links_below.size() < LayerSlice::LinksStaticSize)
                                take_below = false;
                            else if (links_above.size() >= LayerSlice::LinksStaticSize)
                            {
                                size_t shift_below = links_below.end() - it_below;
                                size_t shift_above = links_above.end() - it_above;
                                take_below = shift_below < shift_above;
                            }
                            if (take_below)
                                links_below.insert(it_below, {j, float(area)});
                            else
                                links_above.insert(it_above, {i, float(area)});
                        }
                    }
                }
            }
            for (size_t i = 0; i < polynode.Count(); ++i)
                for (size_t j = 0; j < (*polynode[i]).Count(); ++j)
                    this->visit(*(*polynode[i])[j]);
        }

    private:
        // Find the indices of the contour below & above for an expolygon created as an intersection of two expolygons, one below, the other above.
        // Returns -1 if there is no point on the intersection refering bottom resp. top source expolygon.
        // Returns -2 if the intersection refers to multiple source expolygons on bottom resp. top layers.
        std::pair<int32_t, int32_t> find_top_bottom_contour_ids_strict(const Clipper2Lib::PolyPath64 &polynode) const
        {
            // If there is an intersection point, it should indicate which contours (one from layer below, the other from layer above) intersect.
            // Otherwise the contour is fully inside another contour.
            int32_t i = -1, j = -1;
            auto process_i = [&i, &j](coord_t k)
            {
                if (i == -1)
                    i = k;
                else if (i >= 0)
                {
                    if (i != k)
                    {
                        // Error: Intersection contour contains points of two or more source bottom contours.
                        i = -2;
                        if (j == -2)
                            // break
                            return true;
                    }
                }
                else
                    assert(i == -2);
                return false;
            };
            auto process_j = [&i, &j](coord_t k)
            {
                if (j == -1)
                    j = k;
                else if (j >= 0)
                {
                    if (j != k)
                    {
                        // Error: Intersection contour contains points of two or more source top contours.
                        j = -2;
                        if (i == -2)
                            // break
                            return true;
                    }
                }
                else
                    assert(j == -2);
                return false;
            };
            for (int icontour = 0; icontour <= polynode.Count(); ++icontour)
            {
                const Clipper2Lib::Path64 &contour = icontour == 0 ? polynode.Polygon()
                                                                   : polynode[icontour - 1]->Polygon();
                if (contour.size() >= 3)
                {
                    for (const Clipper2Lib::Point64 &pt : contour)
                        if (coord_t k = pt.z; k < 0)
                        {
                            const auto &intersection = m_intersections[-k - 1];
                            assert(intersection.first <= intersection.second);
                            if (intersection.first < m_offset_above ? process_i(intersection.first)
                                                                    : process_j(intersection.first))
                                goto end;
                            if (intersection.second < m_offset_above ? process_i(intersection.second)
                                                                     : process_j(intersection.second))
                                goto end;
                        }
                        else if (k < m_offset_above ? process_i(k) : process_j(k))
                            goto end;
                }
            }
        end:
            return {i, j};
        }

        // Find the indices of the contour below & above for an expolygon created as an intersection of two expolygons, one below, the other above.
        // This variant expects that the source expolygon assingment is not unique, it counts the majority.
        // Returns -1 if there is no point on the intersection refering bottom resp. top source expolygon.
        // Returns -2 if the intersection refers to multiple source expolygons on bottom resp. top layers.
        std::pair<int32_t, int32_t> find_top_bottom_contour_ids_approx(const Clipper2Lib::PolyPath64 &polynode) const
        {
            // 1) Collect histogram of contour references.
            struct HistoEl
            {
                int32_t id;
                int32_t count;
            };
            std::vector<HistoEl> histogram;
            {
                auto increment_counter = [&histogram](const int32_t i)
                {
                    auto it = std::lower_bound(histogram.begin(), histogram.end(), i,
                                               [](auto l, auto r) { return l.id < r; });
                    if (it == histogram.end() || it->id != i)
                        histogram.insert(it, HistoEl{i, int32_t(1)});
                    else
                        ++it->count;
                };
                for (int icontour = 0; icontour <= polynode.Count(); ++icontour)
                {
                    const Clipper2Lib::Path64 &contour = icontour == 0 ? polynode.Polygon()
                                                                       : polynode[icontour - 1]->Polygon();
                    if (contour.size() >= 3)
                    {
                        for (const Clipper2Lib::Point64 &pt : contour)
                            if (coord_t k = pt.z; k < 0)
                            {
                                const auto &intersection = m_intersections[-k - 1];
                                assert(intersection.first <= intersection.second);
                                increment_counter(intersection.first);
                                increment_counter(intersection.second);
                            }
                            else
                                increment_counter(k);
                    }
                }
                assert(!histogram.empty());
            }
            int32_t i = -1;
            int32_t j = -1;
            if (!histogram.empty())
            {
                // 2) Split the histogram to bottom / top.
                auto mid = std::upper_bound(histogram.begin(), histogram.end(), m_offset_above,
                                            [](auto l, auto r) { return l < r.id; });
                // 3) Sort the bottom / top parts separately.
                auto bottom_begin = histogram.begin();
                auto bottom_end = mid;
                auto top_begin = mid;
                auto top_end = histogram.end();
                std::sort(bottom_begin, bottom_end, [](auto l, auto r) { return l.count > r.count; });
                std::sort(top_begin, top_end, [](auto l, auto r) { return l.count > r.count; });
                double i_quality = 0;
                double j_quality = 0;
                if (bottom_begin != bottom_end)
                {
                    i = bottom_begin->id;
                    i_quality = std::next(bottom_begin) == bottom_end
                                    ? std::numeric_limits<double>::max()
                                    : double(bottom_begin->count) / std::next(bottom_begin)->count;
                }
                if (top_begin != top_end)
                {
                    j = top_begin->id;
                    j_quality = std::next(top_begin) == top_end
                                    ? std::numeric_limits<double>::max()
                                    : double(top_begin->count) / std::next(top_begin)->count;
                }
                // Expected to be called only if there are duplicate references to be resolved by the histogram.
                assert(i >= 0 || j >= 0);
                assert(i_quality < std::numeric_limits<double>::max() ||
                       j_quality < std::numeric_limits<double>::max());
                if (i >= 0 && i_quality < j_quality)
                {
                    // Force the caller to resolve the bottom references the costly but robust way.
                    assert(j >= 0);
                    // Twice the number of references for the best contour.
                    assert(j_quality >= 2.);
                    i = -2;
                }
                else if (j >= 0)
                {
                    // Force the caller to resolve the top reference the costly but robust way.
                    assert(i >= 0);
                    // Twice the number of references for the best contour.
                    assert(i_quality >= 2.);
                    j = -2;
                }
            }
            return {i, j};
        }

        static int32_t find_other_contour_costly(const Clipper2Lib::PolyPath64 &polynode, const Layer &other_layer,
                                                 bool other_has_duplicates)
        {
            if (!other_has_duplicates)
            {
                // The contour below is likely completely inside another contour above. Look-it up in the island above.
                Point pt(polynode.Polygon().front().x, polynode.Polygon().front().y);
                for (int i = int(other_layer.lslices_ex.size()) - 1; i >= 0; --i)
                    if (other_layer.lslices_ex[i].bbox.contains(pt) && other_layer.lslices[i].contains(pt))
                        return i;
                // The following shall not happen now as the source expolygons are being shrunk a bit before intersecting,
                // thus each point of each intersection polygon should fit completely inside one of the original (unshrunk) expolygons.
                assert(false);
            }
            // The comment below may not be valid anymore, see the comment above. However the code is used in case the polynode contains multiple references
            // to other_layer expolygons, thus the references are not unique.
            //
            // The check above might sometimes fail when the polygons overlap only on points, which causes the clipper to detect no intersection.
            // The problem happens rarely, mostly on simple polygons (in terms of number of points), but regardless of size!
            // example of failing link on two layers, each with single polygon without holes.
            // layer A = Polygon{(-24931238,-11153865),(-22504249,-8726874),(-22504249,11477151),(-23261469,12235585),(-23752371,12727276),(-25002495,12727276),(-27502745,10227026),(-27502745,-12727274),(-26504645,-12727274)}
            // layer B = Polygon{(-24877897,-11100524),(-22504249,-8726874),(-22504249,11477151),(-23244827,12218916),(-23752371,12727276),(-25002495,12727276),(-27502745,10227026),(-27502745,-12727274),(-26504645,-12727274)}
            // note that first point is not identical, and the check above picks (-24877897,-11100524) as the first contour point (polynode.Contour.front()).
            // that point is sadly slightly outisde of the layer A, so no link is detected, eventhough they are overlaping "completely"
            // polynode.Polygon() returns Path64 (no Z) so we can't use from_zpath
            // Convert Path64 to Points directly
            Points pts;
            pts.reserve(polynode.Polygon().size());
            for (const auto &pt : polynode.Polygon())
                pts.emplace_back(pt.x, pt.y);
            Polygons contour_poly{Polygon{std::move(pts)}};
            BoundingBox contour_aabb{contour_poly.front().points};
            int32_t i_largest = -1;
            double a_largest = 0;
            for (int i = int(other_layer.lslices_ex.size()) - 1; i >= 0; --i)
                if (contour_aabb.overlap(other_layer.lslices_ex[i].bbox))
                    // it is potentially slow, but should be executed rarely
                    if (Polygons overlap = intersection(contour_poly, other_layer.lslices[i]); !overlap.empty())
                    {
                        if (other_has_duplicates)
                        {
                            // Find the contour with the largest overlap. It is expected that the other overlap will be very small.
                            double a = area(overlap);
                            if (a > a_largest)
                            {
                                a_largest = a;
                                i_largest = i;
                            }
                        }
                        else
                        {
                            // Most likely there is just one contour that overlaps, however it is not guaranteed.
                            i_largest = i;
                            break;
                        }
                    }
            assert(i_largest >= 0);
            return i_largest;
        }

        const std::vector<std::pair<coord_t, coord_t>> &m_intersections;
        Layer &m_below;
        Layer &m_above;
        const coord_t m_offset_below;
        const coord_t m_offset_above;
#ifndef NDEBUG
        const coord_t m_offset_end;
#endif // NDEBUG
    } visitor(intersections, below, above, offset_below, offset_above
#ifndef NDEBUG
              ,
              offset_end
#endif // NDEBUG
    );

    for (size_t i = 0; i < polytree.Count(); ++i)
        visitor.visit(*polytree[i]);

#ifndef NDEBUG
    // Verify that only one directional link is stored: either from bottom slice up or from upper slice down.
    for (int32_t islice = 0; islice < below.lslices_ex.size(); ++islice)
    {
        LayerSlice::Links &links1 = below.lslices_ex[islice].overlaps_above;
        for (LayerSlice::Link &link1 : links1)
        {
            LayerSlice::Links &links2 = above.lslices_ex[link1.slice_idx].overlaps_below;
            assert(!std::binary_search(links2.begin(), links2.end(), link1,
                                       [](auto &l, auto &r) { return l.slice_idx < r.slice_idx; }));
        }
    }
    for (int32_t islice = 0; islice < above.lslices_ex.size(); ++islice)
    {
        LayerSlice::Links &links1 = above.lslices_ex[islice].overlaps_below;
        for (LayerSlice::Link &link1 : links1)
        {
            LayerSlice::Links &links2 = below.lslices_ex[link1.slice_idx].overlaps_above;
            assert(!std::binary_search(links2.begin(), links2.end(), link1,
                                       [](auto &l, auto &r) { return l.slice_idx < r.slice_idx; }));
        }
    }
#endif // NDEBUG

    // Scatter the links, but don't sort them yet.
    for (int32_t islice = 0; islice < int32_t(below.lslices_ex.size()); ++islice)
        for (LayerSlice::Link &link : below.lslices_ex[islice].overlaps_above)
            above.lslices_ex[link.slice_idx].overlaps_below.push_back({islice, link.area});
    for (int32_t islice = 0; islice < int32_t(above.lslices_ex.size()); ++islice)
        for (LayerSlice::Link &link : above.lslices_ex[islice].overlaps_below)
            below.lslices_ex[link.slice_idx].overlaps_above.push_back({islice, link.area});
    // Sort the links.
    for (LayerSlice &lslice : below.lslices_ex)
        std::sort(lslice.overlaps_above.begin(), lslice.overlaps_above.end(),
                  [](const LayerSlice::Link &l, const LayerSlice::Link &r) { return l.slice_idx < r.slice_idx; });
    for (LayerSlice &lslice : above.lslices_ex)
        std::sort(lslice.overlaps_below.begin(), lslice.overlaps_below.end(),
                  [](const LayerSlice::Link &l, const LayerSlice::Link &r) { return l.slice_idx < r.slice_idx; });
}

void Layer::build_up_down_graph(Layer &below, Layer &above)
{
    coord_t paths_below_offset = 0;
    ClipperZUtils::ZPaths paths_below = expolygons_to_zpaths_shrunk(below.lslices, paths_below_offset);
    coord_t paths_above_offset = paths_below_offset + coord_t(below.lslices.size());
    ClipperZUtils::ZPaths paths_above = expolygons_to_zpaths_shrunk(above.lslices, paths_above_offset);
#ifndef NDEBUG
    coord_t paths_end = paths_above_offset + coord_t(above.lslices.size());
#endif // NDEBUG

    // With USINGZ enabled, Z values are preserved through clipping operations.
    // Z encodes the source contour index.

    Clipper2Lib::Clipper64 clipper;
    Clipper2Lib::PolyTree64 result;
    ClipperZUtils::ClipperZIntersectionVisitor::Intersections intersections;
    ClipperZUtils::ClipperZIntersectionVisitor visitor(intersections);
    clipper.SetZCallback(visitor.clipper_callback());

    // Convert ZPaths to Paths64 with Z preserved
    Clipper2Lib::Paths64 paths_below_64 = ClipperZUtils::zpaths_to_paths64(paths_below);
    Clipper2Lib::Paths64 paths_above_64 = ClipperZUtils::zpaths_to_paths64(paths_above);

    clipper.AddSubject(paths_below_64);
    clipper.AddClip(paths_above_64);
    clipper.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::NonZero, result);

    // With USINGZ enabled, use the original connect_layer_slices which reads Z directly
    connect_layer_slices(below, above, result, intersections, paths_below_offset, paths_above_offset
#ifndef NDEBUG
                         ,
                         paths_end
#endif // NDEBUG
    );
}

static inline bool layer_needs_raw_backup(const Layer *layer)
{
    return !(layer->regions().size() == 1 &&
             (layer->id() > 0 || layer->object()->config().elefant_foot_compensation.value == 0));
}

void Layer::backup_untyped_slices()
{
    if (layer_needs_raw_backup(this))
    {
        for (LayerRegion *layerm : m_regions)
            layerm->m_raw_slices = to_expolygons(layerm->slices().surfaces);
    }
    else
    {
        assert(m_regions.size() == 1);
        m_regions.front()->m_raw_slices.clear();
    }
}

void Layer::restore_untyped_slices()
{
    if (layer_needs_raw_backup(this))
    {
        for (LayerRegion *layerm : m_regions)
            layerm->m_slices.set(layerm->m_raw_slices, stInternal);
    }
    else
    {
        assert(m_regions.size() == 1);
        m_regions.front()->m_slices.set(this->lslices, stInternal);
    }
}

// Similar to Layer::restore_untyped_slices()
// Keeps detect_surfaces_type() sound when reslicing, which works with typed slices.
// Only resetting layerm->slices if Slice::extra_perimeters is always zero or it will not be used anymore
// after the perimeter generator.
void Layer::restore_untyped_slices_no_extra_perimeters()
{
    if (layer_needs_raw_backup(this))
    {
        for (LayerRegion *layerm : m_regions)
            if (!layerm->region().config().extra_perimeters.value)
                layerm->m_slices.set(layerm->m_raw_slices, stInternal);
    }
    else
    {
        assert(m_regions.size() == 1);
        LayerRegion *layerm = m_regions.front();
        // This optimization is correct, as extra_perimeters are only reused by prepare_infill() with multi-regions.
        //if (! layerm->region().config().extra_perimeters.value)
        layerm->m_slices.set(this->lslices, stInternal);
    }
}

ExPolygons Layer::merged(float offset_scaled) const
{
    assert(offset_scaled >= 0.f);
    // If no offset is set, apply EPSILON offset before union, and revert it afterwards.
    float offset_scaled2 = 0;
    if (offset_scaled == 0.f)
    {
        offset_scaled = float(EPSILON);
        offset_scaled2 = float(-EPSILON);
    }
    Polygons polygons;
    for (LayerRegion *layerm : m_regions)
    {
        const PrintRegionConfig &config = layerm->region().config();
        // Our users learned to bend preFlight to produce empty volumes to act as subtracters. Only add the region if it is non-empty.
        if (config.bottom_solid_layers > 0 || config.top_solid_layers > 0 || config.fill_density > 0. ||
            config.perimeters > 0)
            append(polygons, offset(layerm->slices().surfaces, offset_scaled));
    }
    ExPolygons out = union_ex(polygons);
    if (offset_scaled2 != 0.f)
        out = offset_ex(out, offset_scaled2);
    return out;
}

// If there is any incompatibility, separate LayerRegions have to be created.
inline bool has_compatible_dynamic_overhang_speed(const PrintRegionConfig &config,
                                                  const PrintRegionConfig &other_config)
{
    bool dynamic_overhang_speed_compatibility = config.enable_dynamic_overhang_speeds ==
                                                other_config.enable_dynamic_overhang_speeds;
    if (dynamic_overhang_speed_compatibility && config.enable_dynamic_overhang_speeds)
    {
        dynamic_overhang_speed_compatibility = config.overhang_speed_0 == other_config.overhang_speed_0 &&
                                               config.overhang_speed_1 == other_config.overhang_speed_1 &&
                                               config.overhang_speed_2 == other_config.overhang_speed_2 &&
                                               config.overhang_speed_3 == other_config.overhang_speed_3;
    }

    return dynamic_overhang_speed_compatibility;
}

// If there is any incompatibility, separate LayerRegions have to be created.
inline bool has_compatible_layer_regions(const PrintRegionConfig &config, const PrintRegionConfig &other_config)
{
    return config.perimeter_extruder == other_config.perimeter_extruder &&
           config.perimeters == other_config.perimeters && config.perimeter_speed == other_config.perimeter_speed &&
           config.external_perimeter_speed == other_config.external_perimeter_speed &&
           config.overhangs == other_config.overhangs &&
           config.opt_serialize("perimeter_extrusion_width") ==
               other_config.opt_serialize("perimeter_extrusion_width") &&
           config.external_perimeters_first == other_config.external_perimeters_first &&
           config.infill_overlap == other_config.infill_overlap &&
           has_compatible_dynamic_overhang_speed(config, other_config);
}

// Here the perimeters are created cummulatively for all layer regions sharing the same parameters influencing the perimeters.
// The perimeter paths and the thin fills (ExtrusionEntityCollection) are assigned to the first compatible layer region.
// The resulting fill surface is split back among the originating regions.
void Layer::make_perimeters()
{
    BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id();
    this->invalidate_role_index();

    if (debug_enabled(DBG_PERIMETERS))
    {
        // Per-region fingerprint of the surfaces the perimeter generator receives: compared
        // with SLICE_FP it brackets where two runs on the same input start to differ.
        for (size_t ri = 0; ri < m_regions.size(); ++ri)
        {
            uint64_t h = 1469598103934665603ull;
            size_t pts = 0;
            double area = 0.;
            for (const Surface &s : m_regions[ri]->slices().surfaces)
            {
                area += s.expolygon.area();
                for (const Point &pt : s.expolygon.contour.points)
                {
                    ++pts;
                    for (const coord_t c : {pt.x(), pt.y()})
                        h = (h ^ uint64_t(c)) * 1099511628211ull;
                }
                for (const Polygon &hole : s.expolygon.holes)
                    for (const Point &pt : hole.points)
                    {
                        ++pts;
                        for (const coord_t c : {pt.x(), pt.y()})
                            h = (h ^ uint64_t(c)) * 1099511628211ull;
                    }
            }
            dbg_log(DBG_PERIMETERS, this->print_z, "DETERM",
                    "REGION_FP layer=%zu region=%zu surfaces=%zu pts=%zu area=%.4fmm2 hash=%016llx", this->id(), ri,
                    m_regions[ri]->slices().surfaces.size(), pts, area * 1e-12, (unsigned long long) h);
        }
        // The slices the perimeter generator classifies overhangs (layer below) and bridges (layer
        // above) against.
        for (const int side : {0, 1})
        {
            const Layer *other = side == 0 ? this->lower_layer : this->upper_layer;
            DetermFingerprint fp;
            if (other != nullptr)
                fp.add(other->lslices);
            dbg_log(DBG_PERIMETERS, this->print_z, "DETERM",
                    "PERIM_LOWER_FP layer=%zu side=%s polys=%zu pts=%zu area=%.4fmm2 hash=%016llx ord=%016llx",
                    this->id(), side == 0 ? "lower" : "upper", fp.count, fp.points, fp.area_mm2(),
                    (unsigned long long) fp.hash, (unsigned long long) fp.ordered);
        }
    }

    // keep track of regions whose perimeters we have already generated
    std::vector<unsigned char> done(m_regions.size(), false);
    std::vector<uint32_t> layer_region_ids;
    std::vector<std::pair<ExtrusionRange, ExtrusionRange>> perimeter_and_gapfill_ranges;
    ExPolygons fill_expolygons;
    std::vector<ExPolygonRange> fill_expolygons_ranges;
    SurfacesPtr surfaces_to_merge;
    SurfacesPtr surfaces_to_merge_temp;

    auto layer_region_reset_perimeters = [](LayerRegion &layerm)
    {
        layerm.m_perimeters.clear();
        layerm.m_perimeters_unsplit.clear();
        layerm.m_fills.clear();
        layerm.m_thin_fills.clear();
        layerm.m_fill_expolygons.clear();
        layerm.m_fill_expolygons_bboxes.clear();
        layerm.m_fill_expolygons_composite.clear();
        layerm.m_fill_expolygons_composite_bboxes.clear();
        // When fill_density or the infill settings change, m_fill_surfaces has to be cleared. It
        // holds every surface type (sparse infill, solid infill, top, bottom and the rest) and
        // prepare_fill_surfaces() regenerates it from the density thresholds and the surface
        // classifications. A stale surface crashes or corrupts both sparse and solid fill patterns,
        // because its geometry, indices and spatial structures no longer match after a reslice.
        layerm.m_fill_surfaces.clear();
        layerm.m_fill_surfaces_prepared.clear();
    };

    // Remove layer islands, remove references to perimeters and fills from these layer islands to LayerRegion ExtrusionEntities.
    for (LayerSlice &lslice : this->lslices_ex)
        lslice.islands.clear();

    for (auto it_curr_region = m_regions.cbegin(); it_curr_region != m_regions.cend(); ++it_curr_region)
    {
        const size_t curr_region_id = std::distance(m_regions.cbegin(), it_curr_region);
        if (done[curr_region_id])
        {
            continue;
        }

        LayerRegion &curr_region = **it_curr_region;
        layer_region_reset_perimeters(curr_region);

        if (curr_region.slices().empty())
        {
            continue;
        }

        BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id() << ", region " << curr_region_id;
        done[curr_region_id] = true;
        const PrintRegionConfig &curr_config = curr_region.region().config();

        perimeter_and_gapfill_ranges.clear();
        fill_expolygons.clear();
        fill_expolygons_ranges.clear();
        surfaces_to_merge.clear();

        // Find compatible regions.
        layer_region_ids.clear();
        layer_region_ids.push_back(curr_region_id);

        PerimeterRegions perimeter_regions;
        for (auto it_next_region = std::next(it_curr_region); it_next_region != m_regions.cend(); ++it_next_region)
        {
            const size_t next_region_id = std::distance(m_regions.cbegin(), it_next_region);
            LayerRegion &next_region = **it_next_region;
            const PrintRegionConfig &next_config = next_region.region().config();
            if (next_region.slices().empty())
            {
                continue;
            }

            if (!has_compatible_layer_regions(curr_config, next_config))
            {
                continue;
            }

            // Now, we are sure that we want to merge LayerRegions in any case.
            layer_region_reset_perimeters(next_region);
            layer_region_ids.push_back(next_region_id);
            done[next_region_id] = true;

            // If any parameters affecting just perimeters are incompatible, then we also create PerimeterRegion.
            if (!PerimeterRegion::has_compatible_perimeter_regions(curr_config, next_config))
            {
                perimeter_regions.emplace_back(next_region);
            }
        }

        // When fuzzy skin is painted, we add the painted areas as PerimeterRegions with the fuzzy-enabled
        // config. This allows polygon_segmentation() to apply fuzzy skin to painted perimeter segments
        // without modifying the underlying slice geometry (no "geometry theft").
        if (!this->fuzzy_skin_painted_areas.empty())
        {
            const auto &layer_ranges = m_object->shared_regions()->layer_ranges;
            // Find the layer range for this layer's slice_z
            auto it_layer_range = lower_bound_by_predicate(layer_ranges.begin(), layer_ranges.end(),
                                                           [this](const PrintObjectRegions::LayerRangeRegions &lr)
                                                           { return lr.layer_height_range.second < this->slice_z; });

            if (it_layer_range != layer_ranges.end() && it_layer_range->layer_height_range.first <= this->slice_z &&
                this->slice_z <= it_layer_range->layer_height_range.second)
            {
                // Get the combined slices for the current region(s)
                ExPolygons curr_slices = to_expolygons(curr_region.slices().surfaces);
                BoundingBox curr_slices_bbox = get_extents(curr_slices);
                BoundingBox painted_bbox = get_extents(this->fuzzy_skin_painted_areas);

                // Only process if bounding boxes overlap
                if (curr_slices_bbox.overlap(painted_bbox))
                {
                    for (const auto &fuzzy_region : it_layer_range->fuzzy_skin_painted_regions)
                    {
                        // Create PerimeterRegion for the intersection of painted areas with current slices
                        ExPolygons fuzzy_expolygons = intersection_ex(this->fuzzy_skin_painted_areas, curr_slices);
                        if (!fuzzy_expolygons.empty())
                        {
                            perimeter_regions.emplace_back(fuzzy_region.region, std::move(fuzzy_expolygons));
                        }
                    }
                }
            }
        }

        if (layer_region_ids.size() == 1)
        { // Optimization.
            curr_region.make_perimeters(curr_region.slices(), perimeter_regions, perimeter_and_gapfill_ranges,
                                        fill_expolygons, fill_expolygons_ranges);
            this->sort_perimeters_into_islands(curr_region.slices(), curr_region_id, perimeter_and_gapfill_ranges,
                                               std::move(fill_expolygons), fill_expolygons_ranges, layer_region_ids);
        }
        else
        {
            SurfaceCollection new_slices;
            // Use the region with highest infill rate, as the make_perimeters() function below decides on the gap fill based on the infill existence.
            uint32_t region_id_config = layer_region_ids.front();
            LayerRegion *layerm_config = m_regions[region_id_config];
            {
                // Merge slices (surfaces) according to number of extra perimeters.
                for (uint32_t region_id : layer_region_ids)
                {
                    LayerRegion &layerm = *m_regions[region_id];
                    for (const Surface &surface : layerm.slices())
                        surfaces_to_merge.emplace_back(&surface);
                    if (layerm.region().config().fill_density > layerm_config->region().config().fill_density)
                    {
                        region_id_config = region_id;
                        layerm_config = &layerm;
                    }
                }

                std::sort(surfaces_to_merge.begin(), surfaces_to_merge.end(),
                          [](const Surface *l, const Surface *r) { return l->extra_perimeters < r->extra_perimeters; });
                for (size_t i = 0; i < surfaces_to_merge.size();)
                {
                    size_t j = i;
                    const Surface &first = *surfaces_to_merge[i];
                    size_t extra_perimeters = first.extra_perimeters;
                    for (; j < surfaces_to_merge.size() && surfaces_to_merge[j]->extra_perimeters == extra_perimeters;
                         ++j)
                        ;

                    if (i + 1 == j)
                    {
                        // Nothing to merge, just copy.
                        new_slices.surfaces.emplace_back(*surfaces_to_merge[i]);
                    }
                    else
                    {
                        surfaces_to_merge_temp.assign(surfaces_to_merge.begin() + i, surfaces_to_merge.begin() + j);
                        new_slices.append(offset_ex(surfaces_to_merge_temp, ClipperSafetyOffset), first);
                    }

                    i = j;
                }
            }

            // Try to merge compatible PerimeterRegions.
            if (perimeter_regions.size() > 1)
            {
                PerimeterRegion::merge_compatible_perimeter_regions(perimeter_regions);
            }

            // Make perimeters.
            layerm_config->make_perimeters(new_slices, perimeter_regions, perimeter_and_gapfill_ranges, fill_expolygons,
                                           fill_expolygons_ranges);
            this->sort_perimeters_into_islands(new_slices, region_id_config, perimeter_and_gapfill_ranges,
                                               std::move(fill_expolygons), fill_expolygons_ranges, layer_region_ids);
        }
    }

    if (debug_enabled(DBG_PERIMETERS))
    {
        // Per-region fingerprint of the generated perimeters and gap fills; roles= carries the
        // per-role lengths, so an overhang classification flip shows without a geometry change.
        for (size_t ri = 0; ri < m_regions.size(); ++ri)
        {
            determ_fp_extrusions(DBG_PERIMETERS, this->print_z, "PERIM_OUT_FP", "perimeters", ri,
                                 m_regions[ri]->perimeters());
            determ_fp_extrusions(DBG_PERIMETERS, this->print_z, "PERIM_OUT_FP", "gapfill", ri,
                                 m_regions[ri]->thin_fills());
        }
    }

    this->check_generated_widths_against_warning(true);

    BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id() << " - Done";
}

void Layer::check_generated_widths_against_warning(const bool include_perimeters)
{
    PrintObject *print_object = this->object();
    if (print_object->width_overrun_tripped())
        return;
    const PrintConfig &print_config = print_object->print()->config();

    struct WidthLimit
    {
        double nozzle;
        double warn_pct;
        long limit_pct; // the maximum as the warning reports it, a whole percentage
    };
    auto limit_for = [&print_config](int extruder_1based) -> WidthLimit
    {
        const size_t i = extruder_1based > 0 ? size_t(extruder_1based - 1) : size_t(0);
        const double nozzle = print_config.nozzle_diameter.get_at(i);
        const double pct = print_config.nozzle_width_warning_max.get_at(i);
        return {nozzle, pct, std::lround(pct)};
    };

    // Interlocking perimeters are exempt: their wide bonding beads are the pattern's structure.
    // A fills tree mixes sparse and solid roles printed by possibly different extruders, so the
    // limit is picked per path role. A width is compared as the warning reports it, as a whole
    // percentage of the nozzle diameter, so a bead the message would call 150% never reads as
    // exceeding a 150% maximum: fill spacing adjustment and variable-width walls land a few
    // microns over a configured width routinely, and only a whole percent over is an overrun.
    std::function<bool(const ExtrusionEntity &, const WidthLimit &, const WidthLimit *)> scan =
        [&](const ExtrusionEntity &entity, const WidthLimit &primary, const WidthLimit *sparse_alt) -> bool
    {
        if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity))
        {
            for (const ExtrusionEntity *child : collection->entities)
                if (child != nullptr && scan(*child, primary, sparse_alt))
                    return true;
            return false;
        }
        auto check_path = [&](const ExtrusionPath &path)
        {
            if (path.role().has(ExtrusionRoleModifier::Interlocking))
                return false;
            const WidthLimit &l = sparse_alt != nullptr && path.role().has(ExtrusionRoleModifier::Infill) &&
                                          !path.role().has(ExtrusionRoleModifier::Solid)
                                      ? *sparse_alt
                                      : primary;
            if (const float w = path.width(); std::lround(100. * double(w) / l.nozzle) > l.limit_pct)
            {
                print_object->warn_on_width_overrun(w, l.nozzle, l.warn_pct, this->print_z);
                return true;
            }
            return false;
        };
        if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity))
            return check_path(*path);
        if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(&entity))
        {
            for (const ExtrusionPath &p : multi->paths)
                if (check_path(p))
                    return true;
            return false;
        }
        if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity))
        {
            for (const ExtrusionPath &p : loop->paths)
                if (check_path(p))
                    return true;
            return false;
        }
        return false;
    };

    for (const LayerRegion *layerm : m_regions)
    {
        const PrintRegionConfig &region_config = layerm->region().config();
        const WidthLimit solid = limit_for(region_config.solid_infill_extruder);
        const WidthLimit sparse = limit_for(region_config.infill_extruder);
        if (include_perimeters && scan(layerm->perimeters(), limit_for(region_config.perimeter_extruder), nullptr))
            return;
        if (scan(layerm->fills(), solid, &sparse))
            return;
    }
}

void Layer::sort_perimeters_into_islands(
    // Slices for which perimeters and fill_expolygons were just created.
    // The slices may have been created by merging multiple source slices with the same perimeter parameters.
    const SurfaceCollection &slices,
    // Region where the perimeters, gap fills and fill expolygons are stored.
    const uint32_t region_id,
    // Perimeters and gap fills produced by the perimeter generator for the slices,
    // sorted by the source slices.
    const std::vector<std::pair<ExtrusionRange, ExtrusionRange>> &perimeter_and_gapfill_ranges,
    // Fill expolygons produced for all source slices above.
    ExPolygons &&fill_expolygons,
    // Fill expolygon ranges sorted by the source slices.
    const std::vector<ExPolygonRange> &fill_expolygons_ranges,
    // If the current layer consists of multiple regions, then the fill_expolygons above are split by the source LayerRegion surfaces.
    const std::vector<uint32_t> &layer_region_ids)
{
    assert(perimeter_and_gapfill_ranges.size() == fill_expolygons_ranges.size());
    assert(!layer_region_ids.empty());

    LayerRegion &this_layer_region = *m_regions[region_id];

    // Bounding boxes of fill_expolygons.
    BoundingBoxes fill_expolygons_bboxes;
    fill_expolygons_bboxes.reserve(fill_expolygons.size());
    for (const ExPolygon &expolygon : fill_expolygons)
        fill_expolygons_bboxes.emplace_back(get_extents(expolygon));

    // Take one sample point for each source slice, to be used to sort source slices into layer slices.
    // source slice index + its sample.
    std::vector<std::pair<uint32_t, Point>> perimeter_slices_queue;
    perimeter_slices_queue.reserve(slices.size());
    for (uint32_t islice = 0; islice < uint32_t(slices.size()); ++islice)
    {
        const std::pair<ExtrusionRange, ExtrusionRange> &extrusions = perimeter_and_gapfill_ranges[islice];
        Point sample;
        bool sample_set = false;
        // Take a sample deep inside its island if available. Infills are usually quite far from the island boundary.
        for (uint32_t iexpoly : fill_expolygons_ranges[islice])
            if (const ExPolygon &expoly = fill_expolygons[iexpoly]; !expoly.empty())
            {
                sample = expoly.contour.points[expoly.contour.points.size() / 2];
                sample_set = true;
                break;
            }
        if (!sample_set)
        {
            // If there is no infill, take a sample of some inner perimeter.
            for (uint32_t iperimeter : extrusions.first)
            {
                const ExtrusionEntity &ee = *this_layer_region.perimeters().entities[iperimeter];
                if (ee.is_collection())
                {
                    for (const ExtrusionEntity *ee2 : dynamic_cast<const ExtrusionEntityCollection &>(ee).entities)
                        if (!ee2->role().is_external())
                        {
                            sample = ee2->middle_point();
                            sample_set = true;
                            goto loop_end;
                        }
                }
                else if (!ee.role().is_external())
                {
                    sample = ee.middle_point();
                    sample_set = true;
                    break;
                }
            }
        loop_end:
            if (!sample_set)
            {
                if (!extrusions.second.empty())
                {
                    // If there is no inner perimeter, take a sample of some gap fill extrusion.
                    sample = this_layer_region.thin_fills().entities[*extrusions.second.begin()]->middle_point();
                    sample_set = true;
                }
                if (!sample_set && !extrusions.first.empty())
                {
                    // As a last resort, take a sample of some external perimeter.
                    sample = this_layer_region.perimeters().entities[*extrusions.first.begin()]->middle_point();
                    sample_set = true;
                }
            }
        }
        // There may be a valid empty island.
        // assert(sample_set);
        if (sample_set)
            perimeter_slices_queue.emplace_back(islice, sample);
    }

    // Map of source fill_expolygon into region and fill_expolygon of that region.
    // -1: not set
    struct RegionWithFillIndex
    {
        int region_id{-1};
        int fill_in_region_id{-1};
    };
    std::vector<RegionWithFillIndex> map_expolygon_to_region_and_fill;
    const bool has_multiple_regions = layer_region_ids.size() > 1;
    assert(has_multiple_regions || layer_region_ids.size() == 1);
    // assign fill_surfaces to each layer
    if (!fill_expolygons.empty())
    {
        if (has_multiple_regions)
        {
            // Sort the bounding boxes lexicographically.
            std::vector<uint32_t> fill_expolygons_bboxes_sorted(fill_expolygons_bboxes.size());
            std::iota(fill_expolygons_bboxes_sorted.begin(), fill_expolygons_bboxes_sorted.end(), 0);
            std::sort(fill_expolygons_bboxes_sorted.begin(), fill_expolygons_bboxes_sorted.end(),
                      [&fill_expolygons_bboxes](uint32_t lhs, uint32_t rhs)
                      {
                          const BoundingBox &bbl = fill_expolygons_bboxes[lhs];
                          const BoundingBox &bbr = fill_expolygons_bboxes[rhs];
                          return bbl.min < bbr.min || (bbl.min == bbr.min && bbl.max < bbr.max);
                      });
            map_expolygon_to_region_and_fill.assign(fill_expolygons.size(), {});
            for (uint32_t region_idx : layer_region_ids)
            {
                LayerRegion &l = *m_regions[region_idx];
                l.m_fill_expolygons = intersection_ex(l.slices().surfaces, fill_expolygons);
                l.m_fill_expolygons_bboxes.reserve(l.fill_expolygons().size());
                for (const ExPolygon &expolygon : l.fill_expolygons())
                {
                    BoundingBox bbox = get_extents(expolygon);
                    l.m_fill_expolygons_bboxes.emplace_back(bbox);
                    auto it_bbox = std::lower_bound(fill_expolygons_bboxes_sorted.begin(),
                                                    fill_expolygons_bboxes_sorted.end(), bbox,
                                                    [&fill_expolygons_bboxes](uint32_t lhs, const BoundingBox &bbr)
                                                    {
                                                        const BoundingBox &bbl = fill_expolygons_bboxes[lhs];
                                                        return bbl.min < bbr.min ||
                                                               (bbl.min == bbr.min && bbl.max < bbr.max);
                                                    });
                    if (it_bbox != fill_expolygons_bboxes_sorted.end())
                        if (uint32_t fill_id = *it_bbox; fill_expolygons_bboxes[fill_id] == bbox)
                        {
                            // With a very high probability the two expolygons match exactly. Confirm that.
                            if (expolygons_match(expolygon, fill_expolygons[fill_id]))
                            {
                                RegionWithFillIndex &ref = map_expolygon_to_region_and_fill[fill_id];
                                // Only one expolygon produced by intersection with LayerRegion surface may match an expolygon of fill_expolygons.
                                assert(ref.region_id == -1 && ref.fill_in_region_id == -1);
                                ref.region_id = region_idx;
                                ref.fill_in_region_id = int(&expolygon - l.fill_expolygons().data());
                            }
                        }
                }
            }
            // Check whether any island contains multiple fills that fall into the same region, but not they are not contiguous.
            // If so, sort fills in that particular region so that fills of an island become contiguous.
            // Index of a region to sort.
            int sort_region_id = -1;
            // Temporary vector of fills for reordering.
            ExPolygons fills_temp;
            // Temporary vector of fill_bboxes for reordering.
            BoundingBoxes fill_bboxes_temp;
            // Vector of new positions of the above.
            std::vector<int> new_positions;
            do
            {
                sort_region_id = -1;
                for (size_t source_slice_idx = 0; source_slice_idx < fill_expolygons_ranges.size(); ++source_slice_idx)
                    if (const ExPolygonRange fill_range = fill_expolygons_ranges[source_slice_idx];
                        fill_range.size() > 1)
                    {
                        // More than one expolygon exists for a single island. Check whether they are contiguous inside a single LayerRegion::fill_expolygons() vector.
                        uint32_t fill_idx = *fill_range.begin();
                        if (const int fill_regon_id = map_expolygon_to_region_and_fill[fill_idx].region_id;
                            fill_regon_id != -1)
                        {
                            int fill_in_region_id = map_expolygon_to_region_and_fill[fill_idx].fill_in_region_id;
                            bool needs_sorting = false;
                            for (++fill_idx; fill_idx != *fill_range.end(); ++fill_idx)
                            {
                                if (const RegionWithFillIndex &ref = map_expolygon_to_region_and_fill[fill_idx];
                                    ref.region_id != fill_regon_id)
                                {
                                    // This island has expolygons split among multiple regions.
                                    needs_sorting = false;
                                    break;
                                }
                                else if (ref.fill_in_region_id != ++fill_in_region_id)
                                {
                                    // This island has all expolygons stored inside the same region, but not sorted.
                                    needs_sorting = true;
                                }
                            }
                            if (needs_sorting)
                            {
                                sort_region_id = fill_regon_id;
                                break;
                            }
                        }
                    }
                if (sort_region_id != -1)
                {
                    // Reorder fills in region with sort_region index.
                    LayerRegion &layerm = *m_regions[sort_region_id];
                    new_positions.assign(layerm.fill_expolygons().size(), -1);
                    int last = 0;
                    for (RegionWithFillIndex &ref : map_expolygon_to_region_and_fill)
                        if (ref.region_id == sort_region_id)
                        {
                            new_positions[ref.fill_in_region_id] = last;
                            ref.fill_in_region_id = last++;
                        }
                    for (auto &new_pos : new_positions)
                        if (new_pos == -1)
                            // Not referenced by any map_expolygon_to_region_and_fill.
                            new_pos = last++;
                    // Move just the content of m_fill_expolygons to fills_temp, but don't move the container vector.
                    auto &fills = layerm.m_fill_expolygons;
                    auto &fill_bboxes = layerm.m_fill_expolygons_bboxes;

                    assert(fills.size() == fill_bboxes.size());
                    assert(last == int(fills.size()));

                    fills_temp.resize(fills.size());
                    fills_temp.assign(std::make_move_iterator(fills.begin()), std::make_move_iterator(fills.end()));

                    fill_bboxes_temp.resize(fill_bboxes.size());
                    fill_bboxes_temp.assign(std::make_move_iterator(fill_bboxes.begin()),
                                            std::make_move_iterator(fill_bboxes.end()));

                    // Move / reorder the ExPolygons and BoundingBoxes back into m_fill_expolygons and m_fill_expolygons_bboxes.
                    for (size_t old_pos = 0; old_pos < new_positions.size(); ++old_pos)
                    {
                        fills[new_positions[old_pos]] = std::move(fills_temp[old_pos]);
                        fill_bboxes[new_positions[old_pos]] = std::move(fill_bboxes_temp[old_pos]);
                    }
                }
            } while (sort_region_id != -1);
        }
        else
        {
            this_layer_region.m_fill_expolygons = std::move(fill_expolygons);
            this_layer_region.m_fill_expolygons_bboxes = std::move(fill_expolygons_bboxes);
        }
    }

    auto insert_into_island = [
                                  // Region where the perimeters, gap fills and fill expolygons are stored.
                                  region_id,
                                  // Whether there are infills with different regions generated for this LayerSlice.
                                  has_multiple_regions,
                                  // Layer split into surfaces
                                  &slices,
                                  // Perimeters and gap fills to be sorted into islands.
                                  &perimeter_and_gapfill_ranges,
                                  // Infill regions to be sorted into islands.
                                  &fill_expolygons, &fill_expolygons_bboxes, &fill_expolygons_ranges,
                                  // Mapping of fill_expolygon to region and its infill.
                                  &map_expolygon_to_region_and_fill,
                                      // Output
                                      &regions = m_regions,
                                  &lslices_ex = this->lslices_ex](int lslice_idx, int source_slice_idx)
    {
        lslices_ex[lslice_idx].islands.push_back({});
        LayerIsland &island = lslices_ex[lslice_idx].islands.back();
        island.perimeters = LayerExtrusionRange(region_id, perimeter_and_gapfill_ranges[source_slice_idx].first);
        island.boundary = slices.surfaces[source_slice_idx].expolygon;
        island.thin_fills = perimeter_and_gapfill_ranges[source_slice_idx].second;
        if (ExPolygonRange fill_range = fill_expolygons_ranges[source_slice_idx]; !fill_range.empty())
        {
            if (has_multiple_regions)
            {
                // Check whether the fill expolygons of this island were split into multiple regions.
                island.fill_region_id = LayerIsland::fill_region_composite_id;
                for (uint32_t fill_idx : fill_range)
                {
                    if (const int fill_regon_id = map_expolygon_to_region_and_fill[fill_idx].region_id;
                        fill_regon_id == -1 || (island.fill_region_id != LayerIsland::fill_region_composite_id &&
                                                int(island.fill_region_id) != fill_regon_id))
                    {
                        island.fill_region_id = LayerIsland::fill_region_composite_id;
                        break;
                    }
                    else
                        island.fill_region_id = fill_regon_id;
                }
                if (island.fill_expolygons_composite())
                {
                    // They were split, thus store the unsplit "composite" expolygons into the region of perimeters.
                    LayerRegion &this_layer_region = *regions[region_id];
                    auto begin = uint32_t(this_layer_region.fill_expolygons_composite().size());
                    this_layer_region.m_fill_expolygons_composite.reserve(
                        this_layer_region.fill_expolygons_composite().size() + fill_range.size());
                    std::move(fill_expolygons.begin() + *fill_range.begin(),
                              fill_expolygons.begin() + *fill_range.end(),
                              std::back_inserter(this_layer_region.m_fill_expolygons_composite));
                    this_layer_region.m_fill_expolygons_composite_bboxes.insert(
                        this_layer_region.m_fill_expolygons_composite_bboxes.end(),
                        fill_expolygons_bboxes.begin() + *fill_range.begin(),
                        fill_expolygons_bboxes.begin() + *fill_range.end());
                    island.fill_expolygons =
                        ExPolygonRange(begin, uint32_t(this_layer_region.fill_expolygons_composite().size()));
                }
                else
                {
                    // All expolygons are stored inside a single LayerRegion in a contiguous range.
                    island.fill_expolygons =
                        ExPolygonRange(map_expolygon_to_region_and_fill[*fill_range.begin()].fill_in_region_id,
                                       map_expolygon_to_region_and_fill[*fill_range.end() - 1].fill_in_region_id + 1);
                }
            }
            else
            {
                // Layer island is made of one fill region only.
                island.fill_expolygons = fill_range;
                island.fill_region_id = region_id;
            }
        }
    };

    // First sort into islands using exact fit.
    // Traverse the slices in an increasing order of bounding box size, so that the islands inside another islands are tested first,
    // so we can just test a point inside ExPolygon::contour and we may skip testing the holes.
    auto point_inside_surface =
        [&lslices = this->lslices, &lslices_ex = this->lslices_ex](size_t lslice_idx, const Point &point)
    {
        const BoundingBox &bbox = lslices_ex[lslice_idx].bbox;
        return point.x() >= bbox.min.x() && point.x() < bbox.max.x() && point.y() >= bbox.min.y() &&
               point.y() < bbox.max.y() &&
               // Exact match: Don't just test whether a point is inside the outer contour of an island,
               // test also whether the point is not inside some hole of the same expolygon.
               // This is unfortunatelly necessary because the point may be inside an expolygon of one of this expolygon's hole
               // and missed due to numerical issues.
               lslices[lslice_idx].contains(point);
    };
    for (int lslice_idx = int(lslices_ex.size()) - 1; lslice_idx >= 0 && !perimeter_slices_queue.empty(); --lslice_idx)
        for (auto it_source_slice = perimeter_slices_queue.begin(); it_source_slice != perimeter_slices_queue.end();
             ++it_source_slice)
            if (point_inside_surface(lslice_idx, it_source_slice->second))
            {
                insert_into_island(lslice_idx, it_source_slice->first);
                if (std::next(it_source_slice) != perimeter_slices_queue.end())
                    // Remove the current slice & point pair from the queue.
                    *it_source_slice = perimeter_slices_queue.back();
                perimeter_slices_queue.pop_back();
                break;
            }
    if (!perimeter_slices_queue.empty())
    {
        // If the slice sample was not fitted into any slice using exact fit, try to find a closest island as a last resort.
        // This should be a rare event especially if the sample point was taken from infill or inner perimeter,
        // however we may land here for external perimeter only islands with fuzzy skin applied.
        // Check whether fuzzy skin was enabled and adjust the bounding box accordingly.
        const PrintObjectConfig &object_config = this->object()->config();
        const PrintRegionConfig &region_config = this_layer_region.region().config();
        const auto bbox_eps = scaled<coord_t>(
            EPSILON + object_config.gcode_resolution.value +
            (region_config.fuzzy_skin.value == FuzzySkinType::None
                 ? 0.
                 : region_config.fuzzy_skin_thickness.value
                       // Fuzzy skin can push a point out by up to one point distance beyond the thickness.
                       + region_config.fuzzy_skin_point_dist.value));
        auto point_inside_surface_dist2 = [&lslices = this->lslices, &lslices_ex = this->lslices_ex,
                                           bbox_eps](const size_t lslice_idx, const Point &point)
        {
            const BoundingBox &bbox = lslices_ex[lslice_idx].bbox;
            return point.x() < bbox.min.x() - bbox_eps || point.x() > bbox.max.x() + bbox_eps ||
                           point.y() < bbox.min.y() - bbox_eps || point.y() > bbox.max.y() + bbox_eps
                       ? std::numeric_limits<double>::max()
                       : (lslices[lslice_idx].point_projection(point) - point).cast<double>().squaredNorm();
        };
        for (auto it_source_slice = perimeter_slices_queue.begin(); it_source_slice != perimeter_slices_queue.end();
             ++it_source_slice)
        {
            double d2min = std::numeric_limits<double>::max();
            int lslice_idx_min = -1;
            for (int lslice_idx = int(lslices_ex.size()) - 1; lslice_idx >= 0; --lslice_idx)
                if (double d2 = point_inside_surface_dist2(lslice_idx, it_source_slice->second); d2 < d2min)
                {
                    d2min = d2;
                    lslice_idx_min = lslice_idx;
                }
            if (lslice_idx_min == -1)
            {
                // This should not happen, but Arachne seems to produce a perimeter point far outside its source contour.
                // As a last resort, find the closest source contours to the sample point.
                for (int lslice_idx = int(lslices_ex.size()) - 1; lslice_idx >= 0; --lslice_idx)
                    if (double d2 = (lslices[lslice_idx].point_projection(it_source_slice->second) -
                                     it_source_slice->second)
                                        .cast<double>()
                                        .squaredNorm();
                        d2 < d2min)
                    {
                        d2min = d2;
                        lslice_idx_min = lslice_idx;
                    }
            }
            assert(lslice_idx_min != -1);
            insert_into_island(lslice_idx_min, it_source_slice->first);
        }
    }
}

void Layer::export_region_slices_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices())
            bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices())
            svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void Layer::export_region_slices_to_svg_debug(const char *name) const
{
    static size_t idx = 0;
    this->export_region_slices_to_svg(debug_out_path("Layer-slices-%s-%d.svg", name, idx++).c_str());
}

void Layer::export_region_fill_surfaces_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices())
            bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto *region : m_regions)
        for (const auto &surface : region->slices())
            svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void Layer::export_region_fill_surfaces_to_svg_debug(const char *name) const
{
    static size_t idx = 0;
    this->export_region_fill_surfaces_to_svg(debug_out_path("Layer-fill_surfaces-%s-%d.svg", name, idx++).c_str());
}

BoundingBox get_extents(const LayerRegion &layer_region)
{
    BoundingBox bbox;
    if (!layer_region.slices().empty())
    {
        bbox = get_extents(layer_region.slices().surfaces.front());
        for (auto it = layer_region.slices().surfaces.cbegin() + 1; it != layer_region.slices().surfaces.cend(); ++it)
            bbox.merge(get_extents(*it));
    }
    return bbox;
}

BoundingBox get_extents(const LayerRegionPtrs &layer_regions)
{
    BoundingBox bbox;
    if (!layer_regions.empty())
    {
        bbox = get_extents(*layer_regions.front());
        for (auto it = layer_regions.begin() + 1; it != layer_regions.end(); ++it)
            bbox.merge(get_extents(**it));
    }
    return bbox;
}

// ============================================================================
// RoleIndex Method Implementations
// ============================================================================

void Layer::RoleIndex::build_from_layer(const Layer *layer)
{
    interlocking_zone.clear();
    interlocking_zone_index.clear();
    bridge_zone.clear();
    bridge_zone_index.clear();

    if (!layer)
        return;

    // The perimeter width sizes the gap fill that merges the interlocking beads into one zone.
    double perimeter_width_mm = 0.0;

    if (!layer->regions().empty())
    {
        // Get perimeter width from layer's first region
        const LayerRegion *first_region = layer->regions()[0];
        const Flow perimeter_flow = first_region->flow(frPerimeter);
        perimeter_width_mm = perimeter_flow.width();
    }
    else if (layer->object())
    {
        // Fallback: Calculate perimeter width from print config
        const PrintObject *obj = layer->object();
        const PrintConfig &print_config = obj->print()->config();

        // Get configured perimeter width from first region if available
        double width = 0.0;
        const PrintObjectRegions *shared_regions = obj->shared_regions();
        if (shared_regions && !shared_regions->all_regions.empty())
        {
            width = shared_regions->all_regions[0]->config().perimeter_extrusion_width.value;
        }

        if (width == 0.0)
        {
            // Auto mode: width = nozzle_diameter
            width = print_config.nozzle_diameter.get_at(0);
        }

        perimeter_width_mm = width;
    }
    else
    {
        // No region gave a width, so fall back to a standard nozzle
        perimeter_width_mm = 0.4;
    }

    // The interlocking perimeter centerlines by width, from the fills and the perimeters of
    // every island. Only this role is queried, so no other extrusion is offset.
    WidthBatches batches;
    for (const LayerRegion *layerm : layer->regions())
        for (const ExtrusionEntity *entity : layerm->fills().entities)
            collect_interlocking_paths(entity, batches);
    for (const LayerSlice &slice : layer->lslices_ex)
        for (const LayerIsland &island : slice.islands)
            if (const LayerRegion *layerm = layer->get_region(island.perimeters.region()))
                for (size_t perimeter_idx : island.perimeters)
                    collect_interlocking_paths(layerm->perimeters().entities[perimeter_idx], batches);

    // One offset per width group covers every path of the group with its half width; every
    // output polygon (holes included) is kept as its own bead, as a per-path offset would
    // produce.
    ExPolygons beads;
    for (auto &[width, polylines] : batches)
        for (Polygon &poly : offset(polylines, scale_(width / 2.0)))
            beads.emplace_back(std::move(poly));

    if (!beads.empty())
    {
        beads = union_ex(beads);
        // The beads have gaps between them; a morphological close by 3x the perimeter width
        // (the pattern's gapped spacing is ~2.4x with edge gaps ~1.0-1.1x, and beads shift
        // between layers) turns them into one zone that treats the gaps as inside.
        coord_t gap_fill = scale_(perimeter_width_mm * 3.0);
        Polygons expanded = offset(to_polygons(beads), gap_fill);
        Polygons filled = offset(expanded, -gap_fill);
        interlocking_zone = union_ex(filled);
    }
    interlocking_zone_index.build(interlocking_zone);

    // Build bridge zone for over-bridge speed detection
    // Collect stBottomBridge and stInternalBridge surfaces and expand by 1mm
    // (same expansion as separate_infill_above_bridges uses)
    {
        ExPolygons bridges;
        for (const LayerRegion *layerm : layer->regions())
            for (const Surface &surface : layerm->fill_surfaces())
                if (surface.surface_type == stBottomBridge || surface.surface_type == stInternalBridge)
                    bridges.push_back(surface.expolygon);

        if (!bridges.empty())
            bridge_zone = offset_ex(union_ex(bridges), scale_(1.0));
        else
            bridge_zone.clear();
        bridge_zone_index.build(bridge_zone);
    }
}

void Layer::RoleIndex::collect_interlocking_paths(const ExtrusionEntity *entity, WidthBatches &batches)
{
    if (!entity)
        return;

    if (const ExtrusionEntityCollection *collection = dynamic_cast<const ExtrusionEntityCollection *>(entity))
    {
        for (const ExtrusionEntity *member : collection->entities)
            collect_interlocking_paths(member, batches);
    }
    else if (const ExtrusionLoop *loop = dynamic_cast<const ExtrusionLoop *>(entity))
    {
        for (const ExtrusionPath &path : loop->paths)
            add_interlocking_path(path, batches);
    }
    else if (const ExtrusionMultiPath *multipath = dynamic_cast<const ExtrusionMultiPath *>(entity))
    {
        for (const ExtrusionPath &path : multipath->paths)
            add_interlocking_path(path, batches);
    }
    else if (const ExtrusionPath *path = dynamic_cast<const ExtrusionPath *>(entity))
    {
        add_interlocking_path(*path, batches);
    }
}

void Layer::RoleIndex::add_interlocking_path(const ExtrusionPath &path, WidthBatches &batches)
{
    if (path.role() != ExtrusionRole::InterlockingPerimeter || path.polyline.points.size() < 2)
        return;

    const float width = path.width();
    auto it = std::find_if(batches.begin(), batches.end(), [width](const auto &b) { return b.first == width; });
    if (it == batches.end())
        it = batches.emplace(batches.end(), width, Polylines());
    it->second.push_back(path.polyline);
}

void Layer::RoleIndex::ZoneIndex::clear()
{
    polygons.clear();
    bbox = BoundingBox();
    cell_size = 0;
    cols = 0;
    rows = 0;
    cells.clear();
}

void Layer::RoleIndex::ZoneIndex::build(const ExPolygons &zone)
{
    clear();
    polygons.reserve(zone.size());
    for (const ExPolygon &expoly : zone)
    {
        ZonePolygon zp;
        zp.bbox = get_extents(expoly.contour);
        zp.contour = Points_to_ClipperPath(expoly.contour.points);
        zp.holes.reserve(expoly.holes.size());
        for (const Polygon &hole : expoly.holes)
            zp.holes.push_back(Points_to_ClipperPath(hole.points));
        bbox.merge(zp.bbox);
        polygons.push_back(std::move(zp));
    }
    if (polygons.empty())
        return;

    // 0.5 mm cells, coarser when the extent would exceed the cell budget.
    constexpr double max_cells = 32768.0;
    const double extent_x = double(bbox.max.x() - bbox.min.x()) + 1.0;
    const double extent_y = double(bbox.max.y() - bbox.min.y()) + 1.0;
    cell_size = std::max(scaled<coord_t>(0.5), coord_t(std::ceil(std::sqrt(extent_x * extent_y / max_cells))));
    cols = size_t((bbox.max.x() - bbox.min.x()) / cell_size) + 1;
    rows = size_t((bbox.max.y() - bbox.min.y()) / cell_size) + 1;
    cells.assign(cols * rows, Unknown);

    auto mark_path = [this](const Clipper2Lib::Path64 &path)
    {
        for (size_t i = 0, n = path.size(); i < n; ++i)
        {
            const Clipper2Lib::Point64 &a = path[i];
            const Clipper2Lib::Point64 &b = path[(i + 1) % n];
            mark_edge(Point(a.x, a.y), Point(b.x, b.y));
        }
    };
    for (const ZonePolygon &zp : polygons)
    {
        mark_path(zp.contour);
        for (const Clipper2Lib::Path64 &hole : zp.holes)
            mark_path(hole);
    }

    // The uncrossed cells form regions bounded by crossed cells; every cell of a region has
    // the same answer, so one polygon test at a region's first cell classifies the region.
    std::vector<size_t> stack;
    for (size_t idx = 0; idx < cells.size(); ++idx)
    {
        if (cells[idx] != Unknown)
            continue;
        const size_t col = idx % cols;
        const size_t row = idx / cols;
        const Point center(bbox.min.x() + coord_t(col) * cell_size + cell_size / 2,
                           bbox.min.y() + coord_t(row) * cell_size + cell_size / 2);
        const uint8_t state = polygons_contain(center) ? Inside : Outside;
        cells[idx] = state;
        stack.push_back(idx);
        while (!stack.empty())
        {
            const size_t cur = stack.back();
            stack.pop_back();
            const size_t c = cur % cols;
            const size_t r = cur / cols;
            auto visit = [this, state, &stack](size_t neighbour)
            {
                if (cells[neighbour] == Unknown)
                {
                    cells[neighbour] = state;
                    stack.push_back(neighbour);
                }
            };
            if (c > 0)
                visit(cur - 1);
            if (c + 1 < cols)
                visit(cur + 1);
            if (r > 0)
                visit(cur - cols);
            if (r + 1 < rows)
                visit(cur + cols);
        }
    }
}

void Layer::RoleIndex::ZoneIndex::mark_cells(size_t col0, size_t col1, size_t row0, size_t row1)
{
    for (size_t row = row0; row <= row1; ++row)
        for (size_t col = col0; col <= col1; ++col)
            cells[row * cols + col] = Crossed;
}

// Marks every closed cell the segment touches. One unit of margin on each side puts a vertex
// or a crossing that lies exactly on a cell boundary into both cells; the per-column y range
// is rounded outward, so floating-point rounding can only add cells.
void Layer::RoleIndex::ZoneIndex::mark_edge(const Point &a, const Point &b)
{
    auto cell_of = [this](coord_t v, coord_t origin, size_t n) -> size_t
    {
        const coord_t d = v - origin;
        return d < 0 ? 0 : std::min(n - 1, size_t(d / cell_size));
    };
    const coord_t x0 = std::min(a.x(), b.x()) - 1;
    const coord_t x1 = std::max(a.x(), b.x()) + 1;
    const coord_t y0 = std::min(a.y(), b.y()) - 1;
    const coord_t y1 = std::max(a.y(), b.y()) + 1;
    const size_t col0 = cell_of(x0, bbox.min.x(), cols);
    const size_t col1 = cell_of(x1, bbox.min.x(), cols);
    if (a.x() == b.x() || col0 == col1)
    {
        mark_cells(col0, col1, cell_of(y0, bbox.min.y(), rows), cell_of(y1, bbox.min.y(), rows));
        return;
    }
    const Point &p = a.x() < b.x() ? a : b;
    const Point &q = a.x() < b.x() ? b : a;
    const double slope = double(q.y() - p.y()) / double(q.x() - p.x());
    for (size_t col = col0; col <= col1; ++col)
    {
        const coord_t cx0 = std::max(p.x(), bbox.min.x() + coord_t(col) * cell_size);
        const coord_t cx1 = std::min(q.x(), bbox.min.x() + coord_t(col + 1) * cell_size);
        const double ya = double(p.y()) + slope * double(cx0 - p.x());
        const double yb = double(p.y()) + slope * double(cx1 - p.x());
        const coord_t ylo = coord_t(std::floor(std::min(ya, yb))) - 1;
        const coord_t yhi = coord_t(std::ceil(std::max(ya, yb))) + 1;
        mark_cells(col, col, cell_of(ylo, bbox.min.y(), rows), cell_of(yhi, bbox.min.y(), rows));
    }
}

// ExPolygon::contains(pt) over the prepared polygons: a point on the contour is inside, a
// point inside a hole is outside, a point on a hole's boundary is inside.
bool Layer::RoleIndex::ZoneIndex::polygons_contain(const Point &pt) const
{
    const Clipper2Lib::Point64 cpt(pt.x(), pt.y());
    for (const ZonePolygon &zp : polygons)
    {
        if (!zp.bbox.contains(pt))
            continue;
        const Clipper2Lib::PointInPolygonResult in_contour = Clipper2Lib::PointInPolygon(cpt, zp.contour);
        if (in_contour == Clipper2Lib::PointInPolygonResult::IsOn)
            return true;
        if (in_contour != Clipper2Lib::PointInPolygonResult::IsInside)
            continue;
        bool in_hole = false;
        for (const Clipper2Lib::Path64 &hole : zp.holes)
            if (Clipper2Lib::PointInPolygon(cpt, hole) == Clipper2Lib::PointInPolygonResult::IsInside)
            {
                in_hole = true;
                break;
            }
        if (!in_hole)
            return true;
    }
    return false;
}

bool Layer::RoleIndex::ZoneIndex::contains(const Point &pt) const
{
    // Outside the zone's extent the polygon test would skip every polygon.
    if (polygons.empty() || !bbox.contains(pt))
        return false;
    const size_t col = std::min(cols - 1, size_t((pt.x() - bbox.min.x()) / cell_size));
    const size_t row = std::min(rows - 1, size_t((pt.y() - bbox.min.y()) / cell_size));
    const uint8_t state = cells[row * cols + col];
    if (state == Crossed)
        return polygons_contain(pt);
    return state == Inside;
}

// ============================================================================
// Layer Context API Implementation
// ============================================================================

const Layer::RoleIndex &Layer::role_index() const
{
    std::lock_guard<std::mutex> lock(m_role_index_mutex);
    if (!m_role_index)
    {
        auto index = std::make_unique<RoleIndex>();
        index->build_from_layer(this);
        m_role_index = std::move(index);
    }
    return *m_role_index;
}

const Layer::RoleIndex &Layer::get_role_index_for_layer(const Layer *layer) const
{
    if (!layer)
    {
        // Return empty index for null layer
        static RoleIndex empty_index;
        return empty_index;
    }
    return layer->role_index();
}

void Layer::invalidate_role_index()
{
    std::lock_guard<std::mutex> lock(m_role_index_mutex);
    m_role_index.reset();
}

} // namespace Luminary
