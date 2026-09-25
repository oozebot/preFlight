///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "luminary/fill/contract/FillBase.hpp"
#include "luminary/layer/print/PrintTypes.hpp"
#include "luminary/geometry/contours/ExPolygon.hpp"
#include "luminary/geometry/contours/Polyline.hpp"
#include "luminary/core/Prelude.hpp"

namespace Luminary
{

class PrintObject;
class Point;

namespace FillLightning
{

GeneratorPtr build_generator(const PrintObject &print_object, const coordf_t fill_density,
                             const std::function<void()> &throw_on_cancel_callback);

class Filler : public Luminary::Fill
{
public:
    ~Filler() override = default;
    bool is_self_crossing() override { return false; }

    Generator *generator{nullptr};

protected:
    Fill *clone() const override { return new Filler(*this); }

    void _fill_surface_single(const FillParams &params, unsigned int thickness_layers,
                              const std::pair<float, Point> &direction, ExPolygon expolygon,
                              Polylines &polylines_out) override;

    // Let the G-code export reoder the infill lines.
    bool no_sort() const override { return false; }
};

} // namespace FillLightning
} // namespace Luminary
