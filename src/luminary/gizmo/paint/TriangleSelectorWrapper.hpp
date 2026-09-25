///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/mesh/paint/TriangleSelector.hpp"
#include "luminary/model/scene/Model.hpp"
#include "luminary/geometry/index/AABBTreeIndirect.hpp"
#include "luminary/geometry/primitives/Point.hpp"

namespace Luminary
{
class TriangleMesh;

// We need to replace the FacetsAnnotation struct for support storage (or extend/add another)
// Problems: Does not support negative volumes, strange usage for supports computed from extrusion -
// expensively converted back to triangles and then sliced again.
// Another problem is weird and very limited interface when painting supports via algorithms

class TriangleSelectorWrapper
{
public:
    const TriangleMesh &mesh;
    const Transform3d &mesh_transform;
    TriangleSelector selector;
    AABBTreeIndirect::Tree<3, float> triangles_tree;

    TriangleSelectorWrapper(const TriangleMesh &mesh, const Transform3d &mesh_transform);

    void enforce_spot(const Vec3f &point, const Vec3f &origin, float radius);
};

} // namespace Luminary
