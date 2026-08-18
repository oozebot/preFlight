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
#include "SplitPromotionBeadingStrategy.hpp"
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
    const int layer_id, const coord_t thin_wall_snap_precision, const coord_t nozzle_diameter,
    const coord_t max_bead_width_external, const double debug_print_z)
{
    // Use internal perimeter spacing/width as the DistributedBeadingStrategy base even with
    // a single perimeter (max_bead_count=2). The wider spacing shifts the skeleton's bead-count
    // transition thresholds, producing mixed lines with non-zero junctions at the 1-to-2 bead
    // boundary. These are extracted as gap-fill perimeters in separateOutInnerContour, honoring
    // the "(minimum)" perimeter contract. The outer bead width is always overridden to
    // ext_perimeter_width by RedistributeBeadingStrategy regardless of the base width here.
    const coord_t use_spacing = max_bead_count <= 1 ? ext_perimeter_spacing : perimeter_spacing;
    const coord_t use_width = max_bead_count <= 1 ? ext_perimeter_width : perimeter_width;
    BeadingStrategyPtr ret = std::make_unique<DistributedBeadingStrategy>(
        use_spacing, use_width, preferred_transition_length, transitioning_angle, wall_split_middle_threshold,
        wall_add_middle_threshold, inward_distributed_center_wall_count);

    std::unique_ptr<RedistributeBeadingStrategy> redistribute;
    if (innermost_spacing > 0)
    {
        // Full constructor with both ext_to_first_internal_spacing and innermost_spacing
        BOOST_LOG_TRIVIAL(trace) << "Applying Redistribute meta-strategy: ext_spacing=" << ext_perimeter_spacing
                                 << ", ext_width=" << ext_perimeter_width
                                 << ", ext_to_first_spacing=" << ext_to_first_internal_spacing
                                 << ", innermost_spacing=" << innermost_spacing
                                 << ", actual_bead_count=" << actual_bead_count;
        redistribute = std::make_unique<RedistributeBeadingStrategy>(ext_perimeter_spacing, ext_perimeter_width,
                                                                     ext_to_first_internal_spacing, innermost_spacing,
                                                                     actual_bead_count, std::move(ret), layer_id);
    }
    else if (ext_to_first_internal_spacing > 0)
    {
        BOOST_LOG_TRIVIAL(trace) << "Applying Redistribute meta-strategy: ext_spacing=" << ext_perimeter_spacing
                                 << ", ext_width=" << ext_perimeter_width
                                 << ", ext_to_first_spacing=" << ext_to_first_internal_spacing;
        redistribute = std::make_unique<RedistributeBeadingStrategy>(ext_perimeter_spacing, ext_perimeter_width,
                                                                     ext_to_first_internal_spacing, std::move(ret),
                                                                     layer_id);
    }
    else
    {
        BOOST_LOG_TRIVIAL(trace) << "Applying Redistribute meta-strategy: ext_spacing=" << ext_perimeter_spacing
                                 << ", ext_width=" << ext_perimeter_width;
        redistribute = std::make_unique<RedistributeBeadingStrategy>(ext_perimeter_spacing, ext_perimeter_width,
                                                                     std::move(ret), layer_id);
    }
    // The 1-vs-2 thin-wall count decision is made by RedistributeBeadingStrategy, so
    // the nozzle anchor for the zero-overlap configuration must live there.
    redistribute->set_nozzle_diameter(nozzle_diameter);
    ret = std::move(redistribute);

    if (print_thin_walls)
    {
        BOOST_LOG_TRIVIAL(trace) << "Applying Widening Beading meta-strategy: min_input=" << min_feature_size
                                 << ", min_output=" << min_bead_width;
        // Pass ext perimeter width and the spacing used by the pre-inset in PerimeterGenerator
        // (ext_perimeter_spacing, the 1st factory param) so the overlap offset correction
        // exactly reverses the pre-inset shrinkage.
        ret = std::make_unique<WideningBeadingStrategy>(std::move(ret), min_feature_size, min_bead_width,
                                                        thin_wall_snap_precision, ext_perimeter_width,
                                                        ext_perimeter_spacing);
    }

    if (outer_wall_offset > 0)
    {
        BOOST_LOG_TRIVIAL(trace) << "Applying OuterWallOffset meta-strategy: offset=" << outer_wall_offset;
        ret = std::make_unique<OuterWallInsetBeadingStrategy>(outer_wall_offset, std::move(ret));
    }

    // Promote center-bead splits to bead-count decisions, so the skeleton smooths
    // them as count transitions instead of receiving per-node width surprises.
    // Applied beneath LimitedBeadingStrategy so a promoted count reaching
    // max_bead_count still receives the 0-width marker wall.
    {
        const coord_t split_ext_width = (ext_perimeter_width > 0) ? ext_perimeter_width : ext_perimeter_spacing;
        const coord_t split_pair_spacing = (ext_to_first_internal_spacing > 0) ? ext_to_first_internal_spacing
                                                                               : ext_perimeter_spacing;
        auto split_promotion = std::make_unique<SplitPromotionBeadingStrategy>(std::move(ret), split_ext_width,
                                                                               ext_perimeter_spacing,
                                                                               split_pair_spacing,
                                                                               max_bead_width_external);
        split_promotion->set_debug_context(debug_print_z, layer_id);
        ret = std::move(split_promotion);
    }

    // Apply the LimitedBeadingStrategy last, since that adds a 0-width marker wall which other beading strategies shouldn't touch.
    BOOST_LOG_TRIVIAL(trace) << "Applying Limited Beading meta-strategy: max_bead_count=" << max_bead_count;
    ret = std::make_unique<LimitedBeadingStrategy>(max_bead_count, std::move(ret), layer_id);
    return ret;
}
} // namespace Slic3r::Athena
