///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/geometry/index/AABBTreeIndirect.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/model/scene/Model.hpp"
#include "admesh/stl.h"

namespace Luminary::Seams::ModelInfo
{
class Painting
{
public:
    Painting(const Transform3d &obj_transform, const ModelVolumePtrs &volumes);

    bool is_enforced(const Vec3f &position, float radius) const;
    bool is_blocked(const Vec3f &position, float radius) const;

private:
    indexed_triangle_set enforcers;
    indexed_triangle_set blockers;
    AABBTreeIndirect::Tree<3, float> enforcers_tree;
    AABBTreeIndirect::Tree<3, float> blockers_tree;
};
} // namespace Luminary::Seams::ModelInfo
