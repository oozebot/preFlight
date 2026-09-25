///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#pragma once

#include "BeadingStrategy.hpp"

namespace Luminary::Athena
{

/*!
 * Absorbs the strategy stack's left-over thickness into the center bead(s), so compute()
 * returns the final width policy and the skeletal trapezoidation never post-processes a
 * beading. Fixed-width beads leave a gap (positive left_over) or an overfill (negative
 * left_over) at the wall center; this strategy widens or contracts the innermost pair (even
 * counts, and the pair flanking a 0-width marker wall) or the center bead (odd counts) and
 * moves their centerlines accordingly. Must be the outermost decorator: it handles the
 * marker wall that LimitedBeadingStrategy inserts.
 */
class CenterAbsorbBeadingStrategy : public BeadingStrategy
{
public:
    /*!
     * \param parent The strategy stack this decorates (must be outermost).
     * \param ext_perimeter_width Actual external perimeter extrusion width.
     * \param ext_perimeter_spacing External spacing used by the pre-inset.
     */
    CenterAbsorbBeadingStrategy(BeadingStrategyPtr parent, coord_t ext_perimeter_width, coord_t ext_perimeter_spacing);

    ~CenterAbsorbBeadingStrategy() override = default;

    Beading compute(coord_t thickness, coord_t bead_count) const override;
    coord_t getOptimalBeadCount(coord_t thickness) const override;
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;
    coord_t getOptimalThickness(coord_t bead_count) const override;
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;
    float getTransitionAnchorPos(coord_t lower_bead_count) const override;
    std::vector<coord_t> getNonlinearThicknesses(coord_t lower_bead_count) const override;
    std::string toString() const override;

    void set_debug_context(double print_z, int layer_id)
    {
        debug_print_z = print_z;
        debug_layer_id = layer_id;
    }

protected:
    // Absorbs \p beading.left_over into its center bead(s) in place and clears left_over.
    void absorb(Beading &beading) const;

    BeadingStrategyPtr parent;
    coord_t ext_perimeter_width;
    coord_t ext_perimeter_spacing;
    double debug_print_z = 0.0;
    int debug_layer_id = -1;
};

} // namespace Luminary::Athena
