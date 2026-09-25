///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include <luminary/geometry/clipper/ClipperUtils.hpp>
#include <luminary/geometry/triangulate/Tesselate.hpp>
#include <algorithm>
#include <iterator>
#include <vector>
#include <cstddef>

#include <luminary/arrange/packing/NFPConcave_Tesselate.hpp>
#include <luminary/arrange/packing/NFP.hpp>

#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/core/Prelude.hpp"

namespace Luminary
{

Polygons convex_decomposition_tess(const Polygon &expoly)
{
    return convex_decomposition_tess(ExPolygon{expoly});
}

Polygons convex_decomposition_tess(const ExPolygon &expoly)
{
    std::vector<Vec2d> tr = Luminary::triangulate_expolygon_2d(expoly);

    auto ret = Luminary::reserve_polygons(tr.size() / 3);
    for (size_t i = 0; i < tr.size(); i += 3)
    {
        ret.emplace_back(Polygon{scaled(tr[i]), scaled(tr[i + 1]), scaled(tr[i + 2])});
    }

    return ret;
}

Polygons convex_decomposition_tess(const ExPolygons &expolys)
{
    constexpr size_t AvgTriangleCountGuess = 50;

    auto ret = reserve_polygons(AvgTriangleCountGuess * expolys.size());
    for (const ExPolygon &expoly : expolys)
    {
        Polygons convparts = convex_decomposition_tess(expoly);
        std::move(convparts.begin(), convparts.end(), std::back_inserter(ret));
    }

    return ret;
}

ExPolygons nfp_concave_concave_tess(const ExPolygon &fixed, const ExPolygon &movable)
{
    Polygons fixed_decomp = convex_decomposition_tess(fixed);
    Polygons movable_decomp = convex_decomposition_tess(movable);

    auto refs_mv = reserve_vector<Vec2crd>(movable_decomp.size());

    for (const Polygon &p : movable_decomp)
        refs_mv.emplace_back(reference_vertex(p));

    auto nfps = reserve_polygons(fixed_decomp.size() * movable_decomp.size());

    Vec2crd ref_whole = reference_vertex(movable);
    for (const Polygon &fixed_part : fixed_decomp)
    {
        size_t mvi = 0;
        for (const Polygon &movable_part : movable_decomp)
        {
            Polygon subnfp = nfp_convex_convex(fixed_part, movable_part);
            const Vec2crd &ref_mp = refs_mv[mvi];
            auto d = ref_whole - ref_mp;
            subnfp.translate(d);
            nfps.emplace_back(subnfp);
            mvi++;
        }
    }

    return union_ex(nfps);
}

} // namespace Luminary
