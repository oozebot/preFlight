///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#ifndef ATHENA_BEADING_STRATEGY_FACTORY_H
#define ATHENA_BEADING_STRATEGY_FACTORY_H

#include <math.h>
#include <cmath>

#include "BeadingStrategy.hpp"
#include "../../Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Athena
{

class BeadingStrategyFactory
{
public:
    static BeadingStrategyPtr makeStrategy(
        coord_t ext_perimeter_spacing = scaled<coord_t>(0.0005), coord_t ext_perimeter_width = scaled<coord_t>(0.0005),
        coord_t perimeter_spacing = scaled<coord_t>(0.0005), coord_t perimeter_width = scaled<coord_t>(0.0005),
        coord_t preferred_transition_length = scaled<coord_t>(0.0004), float transitioning_angle = M_PI / 4.0,
        bool print_thin_walls = false, coord_t min_bead_width = 0, coord_t min_feature_size = 0,
        double wall_split_middle_threshold = 0.5, double wall_add_middle_threshold = 0.5, coord_t max_bead_count = 0,
        coord_t outer_wall_offset = 0, int inward_distributed_center_wall_count = 2,
        coord_t ext_to_first_internal_spacing = 0, // 0 = use ext_perimeter_spacing
        coord_t innermost_spacing = 0,             // 0 = use perimeter_spacing
        coord_t actual_bead_count = 0,             // actual shells requested (not theoretical max)
        int layer_id = -1                          // For debug output (-1 = unknown)
    );
};

} // namespace Slic3r::Athena
#endif // ATHENA_BEADING_STRATEGY_FACTORY_H
