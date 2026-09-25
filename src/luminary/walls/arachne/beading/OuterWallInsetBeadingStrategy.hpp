///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#pragma once

#include <string>

#include "BeadingStrategy.hpp"
#include "luminary/core/Prelude.hpp"

namespace Luminary::Arachne
{
/*
     * This is a meta strategy that allows for the outer wall to be inset towards the inside of the model. 
     */
class OuterWallInsetBeadingStrategy : public BeadingStrategy
{
public:
    OuterWallInsetBeadingStrategy(coord_t outer_wall_offset, BeadingStrategyPtr parent);

    ~OuterWallInsetBeadingStrategy() override = default;

    Beading compute(coord_t thickness, coord_t bead_count) const override;

    coord_t getOptimalThickness(coord_t bead_count) const override;
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;
    coord_t getOptimalBeadCount(coord_t thickness) const override;
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;

    std::string toString() const override;

private:
    BeadingStrategyPtr parent;
    coord_t outer_wall_offset;
};
} // namespace Luminary::Arachne
