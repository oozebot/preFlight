///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "BaobabSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "TreeSupportCommon.hpp"
#include "libslic3r/Print.hpp"

namespace Slic3r
{

namespace
{
// Read a knob override from the environment once, else keep the default. Zero is a valid
// value and disables the knob; only a negative or unparseable value falls back.
double env_knob(const char *name, double fallback)
{
    const char *raw = std::getenv(name);
    if (raw == nullptr)
        return fallback;
    const double parsed = std::atof(raw);
    return parsed >= 0. ? parsed : fallback;
}
} // namespace

bool is_baobab_object(const PrintObject &print_object)
{
    // True when the pass being run is a Baobab pass. A purely Baobab-painted object is
    // Baobab in every pass regardless of the style dropdown. A multi-type object gets one
    // engine pass per painted type, and the dispatcher pins the style to the pass it is
    // running - so on those objects the style is the disambiguator: the shared tree and
    // toolpath code serves organic work under smsOrganic and baobab work under smsBaobab.
    if (print_object.config().support_material_style.value == smsBaobab)
        return true;
    return print_object.has_baobab_enforcers() && !print_object.has_snug_enforcers() &&
           !print_object.has_grid_enforcers() && !print_object.has_organic_enforcers();
}

double baobab_taper_angle_deg()
{
    static const double value = env_knob("PREFLIGHT_BAOBAB_TAPER_DEG", Baobab::taper_angle_deg);
    return value;
}

coord_t baobab_max_growth_per_layer(coord_t support_line_width)
{
    return support_line_width / 2;
}

double baobab_taper_angle_rad(const FFFTreeSupport::TreeSupportMeshGroupSettings &settings)
{
    const double requested_deg = baobab_taper_angle_deg();
    const double requested = requested_deg > 0. ? requested_deg * M_PI / 180.
                                                : settings.support_baobab_max_canopy_angle;
    const double bead_limit = std::atan2(double(baobab_max_growth_per_layer(settings.support_line_width)),
                                         double(settings.layer_height));
    return std::min(requested, bead_limit);
}

Polygon baobab_disk(const Point &center, coord_t radius)
{
    if (radius <= 0)
        return {};
    // Chord tolerance fixes the vertex count, so a disk is as round as it needs to be and no rounder.
    const double tolerance = scaled<double>(0.02);
    const double ratio = std::clamp(1. - tolerance / double(radius), -1., 1.);
    const auto segments = std::clamp<size_t>(size_t(std::ceil(M_PI / std::acos(ratio))), 16, 128);
    Polygon out;
    out.points.reserve(segments);
    for (size_t i = 0; i < segments; ++i)
    {
        const double angle = 2. * M_PI * double(i) / double(segments);
        out.points.emplace_back(
            center + Point(coord_t(double(radius) * std::cos(angle)), coord_t(double(radius) * std::sin(angle))));
    }
    return out;
}

void baobab_apply_tree_settings(FFFTreeSupport::TreeSupportMeshGroupSettings &settings, const PrintObjectConfig &config)
{
    // Baobab has its own settings group: every routing value comes from the support_baobab_*
    // options, so the Organic options cannot retune a baobab object. The environment knobs
    // remain as no-rebuild sweep overrides. Only baobab objects route through this function,
    // so stock organic is untouched.
    settings.support_tree_angle = std::clamp<double>(config.support_baobab_angle * M_PI / 180., 0.,
                                                     0.5 * M_PI - EPSILON);
    settings.support_tree_angle_slow = std::clamp<double>(config.support_baobab_angle_slow * M_PI / 180., 0.,
                                                          settings.support_tree_angle - EPSILON);
    settings.support_tree_branch_diameter = scaled<coord_t>(
        env_knob("PREFLIGHT_BAOBAB_TRUNK_MM", config.support_baobab_trunk_diameter));
    settings.support_tree_branch_diameter_angle = std::clamp<double>(config.support_baobab_trunk_diameter_angle * M_PI /
                                                                         180.,
                                                                     0., 0.5 * M_PI - EPSILON);
    settings.support_tree_branch_distance = scaled<coord_t>(
        env_knob("PREFLIGHT_BAOBAB_GATHER_MM", config.support_baobab_trunk_distance));
    // Branch density has no baobab meaning (tips are seeded by the trunk distance), but it
    // feeds the along-a-line sampling distance, which stays at its stock value: pin it so the
    // Organic option cannot retune it.
    settings.support_tree_top_rate = 15.;
    // Tips are never printed - every interface tip becomes a canopy and the hemisphere is only
    // the rejection fallback - so the tip diameter is derived, not asked: two beads wide,
    // never wider than the trunk.
    settings.support_tree_tip_diameter = std::min(coord_t(2 * settings.support_line_width),
                                                  settings.support_tree_branch_diameter);
    const char *plant_env = std::getenv("PREFLIGHT_BAOBAB_PLANT");
    settings.support_baobab_plant_on_model = plant_env != nullptr ? std::atoi(plant_env) != 0
                                                                  : config.support_baobab_plant_on_model.value;
    // Planting is baobab's own permission to touch the model; buildplate-only stays an
    // auto-supports option and has no say here. support_rests_on_model derives from this
    // field downstream, so overwriting it is the whole decoupling.
    settings.support_material_buildplate_only = !settings.support_baobab_plant_on_model;
}

} // namespace Slic3r
