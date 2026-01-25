///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "BeadingStrategyFactory.hpp"

#include <boost/log/trivial.hpp>
#include <memory>
#include <utility>

#include "LimitedBeadingStrategy.hpp"
#include "WideningBeadingStrategy.hpp"
#include "DistributedBeadingStrategy.hpp"
#include "RedistributeBeadingStrategy.hpp"
#include "OuterWallInsetBeadingStrategy.hpp"
#include "libslic3r/Athena/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Athena
{

BeadingStrategyPtr BeadingStrategyFactory::makeStrategy(
    const coord_t ext_perimeter_spacing, const coord_t ext_perimeter_width, const coord_t perimeter_spacing,
    const coord_t perimeter_width, const coord_t preferred_transition_length, const float transitioning_angle,
    const bool print_thin_walls, const coord_t min_bead_width, const coord_t min_feature_size,
    const double wall_split_middle_threshold, const double wall_add_middle_threshold, const coord_t max_bead_count,
    const coord_t outer_wall_offset, const int inward_distributed_center_wall_count,
    const coord_t ext_to_first_internal_spacing, const coord_t innermost_spacing, const coord_t actual_bead_count,
    const int layer_id) // For debug output
{
    // Handle a special case when there is just one external perimeter.
    const coord_t use_spacing = max_bead_count <= 2 ? ext_perimeter_spacing : perimeter_spacing;
    const coord_t use_width = max_bead_count <= 2 ? ext_perimeter_width : perimeter_width;
    BeadingStrategyPtr ret = std::make_unique<DistributedBeadingStrategy>(
        use_spacing, use_width, preferred_transition_length, transitioning_angle, wall_split_middle_threshold,
        wall_add_middle_threshold, inward_distributed_center_wall_count);

    if (innermost_spacing > 0)
    {
        // Full constructor with both ext_to_first_internal_spacing and innermost_spacing
        BOOST_LOG_TRIVIAL(trace) << "Applying Redistribute meta-strategy: ext_spacing=" << ext_perimeter_spacing
                                 << ", ext_width=" << ext_perimeter_width
                                 << ", ext_to_first_spacing=" << ext_to_first_internal_spacing
                                 << ", innermost_spacing=" << innermost_spacing
                                 << ", actual_bead_count=" << actual_bead_count;
        ret = std::make_unique<RedistributeBeadingStrategy>(ext_perimeter_spacing, ext_perimeter_width,
                                                            ext_to_first_internal_spacing, innermost_spacing,
                                                            actual_bead_count, std::move(ret), layer_id);
    }
    else if (ext_to_first_internal_spacing > 0)
    {
        BOOST_LOG_TRIVIAL(trace) << "Applying Redistribute meta-strategy: ext_spacing=" << ext_perimeter_spacing
                                 << ", ext_width=" << ext_perimeter_width
                                 << ", ext_to_first_spacing=" << ext_to_first_internal_spacing;
        ret = std::make_unique<RedistributeBeadingStrategy>(ext_perimeter_spacing, ext_perimeter_width,
                                                            ext_to_first_internal_spacing, std::move(ret), layer_id);
    }
    else
    {
        BOOST_LOG_TRIVIAL(trace) << "Applying Redistribute meta-strategy: ext_spacing=" << ext_perimeter_spacing
                                 << ", ext_width=" << ext_perimeter_width;
        ret = std::make_unique<RedistributeBeadingStrategy>(ext_perimeter_spacing, ext_perimeter_width, std::move(ret),
                                                            layer_id);
    }

    if (print_thin_walls)
    {
        BOOST_LOG_TRIVIAL(trace) << "Applying Widening Beading meta-strategy: min_input=" << min_feature_size
                                 << ", min_output=" << min_bead_width;
        ret = std::make_unique<WideningBeadingStrategy>(std::move(ret), min_feature_size, min_bead_width);
    }

    if (outer_wall_offset > 0)
    {
        BOOST_LOG_TRIVIAL(trace) << "Applying OuterWallOffset meta-strategy: offset=" << outer_wall_offset;
        ret = std::make_unique<OuterWallInsetBeadingStrategy>(outer_wall_offset, std::move(ret));
    }

    // Apply the LimitedBeadingStrategy last, since that adds a 0-width marker wall which other beading strategies shouldn't touch.
    BOOST_LOG_TRIVIAL(trace) << "Applying Limited Beading meta-strategy: max_bead_count=" << max_bead_count;
    ret = std::make_unique<LimitedBeadingStrategy>(max_bead_count, std::move(ret), layer_id);
    return ret;
}
} // namespace Slic3r::Athena
