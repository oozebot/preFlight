///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2022 Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <vector>

#include "luminary/core/Prelude.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/index/EdgeGrid.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "luminary/layer/model/Layer.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"
#include "luminary/toolpath/ordering/ShortestPath.hpp"

namespace Luminary
{

// Forward declarations.
class GCodeGenerator;
class Layer;
class Point;

class AvoidCrossingPerimeters
{
public:
    // Routing around the objects vs. inside a single object.
    void use_external_mp(bool use = true) { m_use_external_mp = use; };
    bool used_external_mp_once() { return use_external_mp_once; }
    void disable_once() { m_disabled_once = true; }
    bool disabled_once() const { return m_disabled_once; }
    void reset_once_modifiers()
    {
        use_external_mp_once = false;
        m_disabled_once = false;
    }

    void init_layer(const Layer &layer);

    Polyline travel_to(const GCodeGenerator &gcodegen, const Point &point)
    {
        bool could_be_wipe_disabled;
        return this->travel_to(gcodegen, point, &could_be_wipe_disabled);
    }

    Polyline travel_to(const GCodeGenerator &gcodegen, const Point &point, bool *could_be_wipe_disabled);

    struct Boundary
    {
        // Collection of boundaries used for detection of crossing perimeters for travels
        Polygons boundaries;
        // Bounding box of boundaries
        BoundingBoxf bbox;
        // Precomputed distances of all points in boundaries
        std::vector<std::vector<float>> boundaries_params;
        // Used for detection of intersection between line and any polygon from boundaries
        EdgeGrid::Grid grid;

        void clear()
        {
            boundaries.clear();
            boundaries_params.clear();
        }
    };

    // just for the next travel move
    bool use_external_mp_once{false};

private:
    bool m_use_external_mp{false};
    // this flag disables avoid_crossing_perimeters just for the next travel move
    // we enable it by default for the first travel move in print
    bool m_disabled_once{true};

    // Lslices offseted by half an external perimeter width. Used for detection if line or polyline is inside of any polygon.
    ExPolygons m_lslices_offset;
    std::vector<BoundingBox> m_lslices_offset_bboxes;
    // Used for detection of line or polyline is inside of any polygon.
    EdgeGrid::Grid m_grid_lslices_offset;
    // Store all needed data for travels inside object
    Boundary m_internal;
    // Store all needed data for travels outside object
    Boundary m_external;
};

} // namespace Luminary
