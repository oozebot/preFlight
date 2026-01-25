///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#ifndef ATHENA_REDISTRIBUTE_DISTRIBUTED_BEADING_STRATEGY_H
#define ATHENA_REDISTRIBUTE_DISTRIBUTED_BEADING_STRATEGY_H

#include <string>

#include "BeadingStrategy.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Athena
{
/*!
     * A meta-beading-strategy that takes outer and inner wall widths into account.
     *
     * The outer wall will try to keep a constant width by only applying the beading strategy on the inner walls. This
     * ensures that this outer wall doesn't react to changes happening to inner walls. It will limit print artifacts on
     * the surface of the print. Although this strategy technically deviates from the original philosophy of the paper.
     * It will generally results in better prints because of a smoother motion and less variation in extrusion width in
     * the outer walls.
     *
     * If the thickness of the model is less then two times the optimal outer wall width and once the minimum inner wall
     * width it will keep the minimum inner wall at a minimum constant and vary the outer wall widths symmetrical. Until
     * The thickness of the model is that of at least twice the optimal outer wall width it will then use two
     * symmetrical outer walls only. Until it transitions into a single outer wall. These last scenario's are always
     * symmetrical in nature, disregarding the user specified strategy.
     */
class RedistributeBeadingStrategy : public BeadingStrategy
{
public:
    /*!
         * /param ext_perimeter_spacing  Spacing for external perimeter (controls path placement)
         * /param ext_perimeter_width    Actual external perimeter extrusion width
         * /param parent                 Parent strategy that handles internal perimeters
         * /param layer_id                For debug output (-1 = unknown)
         */
    RedistributeBeadingStrategy(coord_t ext_perimeter_spacing, coord_t ext_perimeter_width, BeadingStrategyPtr parent,
                                int layer_id = -1);

    /*!
         * Extended constructor for preFlight precise walls feature
         * /param ext_perimeter_spacing         Spacing for external perimeter
         * /param ext_perimeter_width           Actual external perimeter extrusion width
         * /param ext_to_first_internal_spacing Spacing between external and first internal perimeter
         * /param parent                        Parent strategy that handles internal perimeters
         * /param layer_id                       For debug output (-1 = unknown)
         */
    RedistributeBeadingStrategy(coord_t ext_perimeter_spacing, coord_t ext_perimeter_width,
                                coord_t ext_to_first_internal_spacing, BeadingStrategyPtr parent, int layer_id = -1);

    /*!
         * Full constructor for preFlight interlocking perimeters with innermost spacing control
         * /param ext_perimeter_spacing         Spacing for external perimeter
         * /param ext_perimeter_width           Actual external perimeter extrusion width
         * /param ext_to_first_internal_spacing Spacing between external and first internal perimeter (0 = use ext_perimeter_spacing)
         * /param innermost_spacing              Spacing between second-innermost and innermost perimeter (0 = use bead_spacing)
         * /param max_bead_count                Maximum number of beads that will actually be used (for determining which is innermost)
         * /param parent                        Parent strategy that handles internal perimeters
         * /param layer_id                       For debug output (-1 = unknown)
         */
    RedistributeBeadingStrategy(coord_t ext_perimeter_spacing, coord_t ext_perimeter_width,
                                coord_t ext_to_first_internal_spacing, coord_t innermost_spacing,
                                coord_t max_bead_count, BeadingStrategyPtr parent, int layer_id = -1);

    ~RedistributeBeadingStrategy() override = default;

    Beading compute(coord_t thickness, coord_t bead_count) const override;

    coord_t getOptimalThickness(coord_t bead_count) const override;
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;
    coord_t getOptimalBeadCount(coord_t thickness) const override;
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;
    float getTransitionAnchorPos(coord_t lower_bead_count) const override;

    std::string toString() const override;

protected:
    BeadingStrategyPtr parent;
    coord_t ext_perimeter_spacing;         //! Spacing for external perimeter paths
    coord_t ext_perimeter_width;           //! Extrusion width for external perimeters
    coord_t ext_to_first_internal_spacing; //! Spacing between external and first internal (0 = use ext_perimeter_spacing)
    coord_t innermost_spacing;             //! Spacing between second-innermost and innermost (0 = use bead_spacing)
    coord_t max_bead_count;                //! Maximum number of beads that will be used (determines which is innermost)
    int debug_layer_id;                    //! Layer ID for debug output (-1 = unknown)
};

} // namespace Slic3r::Athena
#endif // ATHENA_INWARD_DISTRIBUTED_BEADING_STRATEGY_H
