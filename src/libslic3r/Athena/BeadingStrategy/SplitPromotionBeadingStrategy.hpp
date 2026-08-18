///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/

#ifndef ATHENA_SPLIT_PROMOTION_BEADING_STRATEGY_H
#define ATHENA_SPLIT_PROMOTION_BEADING_STRATEGY_H

#include "BeadingStrategy.hpp"

namespace Slic3r::Athena
{

/*!
 * Promotes the center-bead split from a per-node width adjustment into a bead-count
 * decision. A wall thick enough that its expanded center bead should split into a
 * pair reports one MORE bead here, and compute() emits the exact split geometry for
 * that promoted count. Count changes are the one kind of discrete decision the
 * skeletal trapezoidation smooths (transition ribs, filtering, anchoring); deciding
 * the split as a width adjustment after beading bypassed all of that, so adjacent
 * nodes could disagree and shred a uniform wall into transition wedges.
 */
class SplitPromotionBeadingStrategy : public BeadingStrategy
{
public:
    /*!
     * \param parent The strategy stack this decorates (must be outermost).
     * \param ext_perimeter_width Actual external perimeter extrusion width.
     * \param ext_perimeter_spacing External spacing used by the pre-inset.
     * \param ext_split_spacing Spacing for split pairs on single-bead walls.
     * \param max_bead_width_external Printable cap forcing a split above it (0 = none).
     */
    SplitPromotionBeadingStrategy(BeadingStrategyPtr parent, coord_t ext_perimeter_width, coord_t ext_perimeter_spacing,
                                  coord_t ext_split_spacing, coord_t max_bead_width_external);

    ~SplitPromotionBeadingStrategy() override = default;

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
    // Structural eligibility for a center-bead split: real beads, odd count, no
    // interlocking position preservation, sane nominal spacing/width.
    bool splitEligible(const Beading &beading) const;

    // Width of each bead of the split pair for \p beading. Pure geometry, continuous
    // in thickness; meaningful whenever splitEligible() holds.
    coord_t splitPairWidth(const Beading &beading) const;

    // Decides whether the center bead of \p beading (parent output for an odd count at
    // \p thickness) splits into a pair, and if so returns the split pair's width.
    // Exact decision the width-adjustment pass used to make, expressed as a pure
    // function of (thickness, parent beading). Drives the COUNT decision only;
    // compute() emits split geometry on provenance alone so its output stays
    // continuous in thickness across this predicate's boundary.
    bool shouldSplit(coord_t thickness, const Beading &beading, coord_t &split_width_out) const;

    BeadingStrategyPtr parent;
    coord_t ext_perimeter_width;
    coord_t ext_perimeter_spacing;
    coord_t ext_split_spacing;
    coord_t max_bead_width_external;
    // Boundary cache for getTransitionThickness (one strategy instance per generate()
    // call, used single-threaded).
    mutable std::vector<std::pair<coord_t, coord_t>> boundary_cache;
    double debug_print_z = 0.0;
    int debug_layer_id = -1;
};

} // namespace Slic3r::Athena
#endif // ATHENA_SPLIT_PROMOTION_BEADING_STRATEGY_H
