///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 - 2023 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
// Tree supports by Thomas Rahm, losely based on Tree Supports by CuraEngine.
// Original source of Thomas Rahm's tree supports:
// https://github.com/ThomasRahm/CuraEngine
//
// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#pragma once

#include <boost/container/small_vector.hpp>
#include <stddef.h>
#include <stdint.h>
#include <boost/cstdint.hpp>
#include <algorithm>
#include <deque>
#include <functional>
#include <limits>
#include <utility>
#include <vector>
#include <cstddef>

#include "luminary/supports/model/SupportLayer.hpp"
#include "TreeModelVolumes.hpp"
#include "TreeSupportCommon.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/geometry/contours/Polygon.hpp"
#include "luminary/core/Prelude.hpp"

// #define TREE_SUPPORT_SHOW_ERRORS

namespace Luminary
{

// Forward declarations
class Print;
class PrintObject;
struct SlicingParameters;

namespace FFFTreeSupport
{

// The number of vertices in each circle.
static constexpr const size_t SUPPORT_TREE_CIRCLE_RESOLUTION = 25;

struct AreaIncreaseSettings
{
    AreaIncreaseSettings(TreeModelVolumes::AvoidanceType type = TreeModelVolumes::AvoidanceType::Fast,
                         coord_t increase_speed = 0, bool increase_radius = false, bool no_error = false,
                         bool use_min_distance = false, bool move = false)
        : increase_speed{increase_speed}
        , type{type}
        , increase_radius{increase_radius}
        , no_error{no_error}
        , use_min_distance{use_min_distance}
        , move{move}
    {
    }

    coord_t increase_speed;
    // Packing for smaller memory footprint of SupportElementState && SupportElementMerging
    TreeModelVolumes::AvoidanceType type;
    bool increase_radius : 1;
    bool no_error : 1;
    bool use_min_distance : 1;
    bool move : 1;
    bool operator==(const AreaIncreaseSettings &other) const
    {
        return type == other.type && increase_speed == other.increase_speed &&
               increase_radius == other.increase_radius && no_error == other.no_error &&
               use_min_distance == other.use_min_distance && move == other.move;
    }
};

#define TREE_SUPPORTS_TRACK_LOST

// C++17 does not support in place initializers of bit values, thus a constructor zeroing the bits is provided.
struct SupportElementStateBits
{
    SupportElementStateBits()
        : to_buildplate(false)
        , to_model_gracious(false)
        , use_min_xy_dist(false)
        , supports_roof(false)
        , can_use_safe_radius(false)
        , skip_ovalisation(false)
        ,
#ifdef TREE_SUPPORTS_TRACK_LOST
        lost(false)
        , verylost(false)
        ,
#endif // TREE_SUPPORTS_TRACK_LOST
        deleted(false)
        , marked(false)
    {
    }

    /*!
     * \brief The element trys to reach the buildplate
     */
    bool to_buildplate : 1;

    /*!
     * \brief Will the branch be able to rest completely on a flat surface, be it buildplate or model ?
     */
    bool to_model_gracious : 1;

    /*!
     * \brief Whether the min_xy_distance can be used to get avoidance or similar. Will only be true if support_xy_overrides_z=Z overrides X/Y.
     */
    bool use_min_xy_dist : 1;

    /*!
     * \brief True if this Element or any parent (element above) provides support to a support roof.
     */
    bool supports_roof : 1;

    /*!
     * \brief An influence area is considered safe when it can use the holefree avoidance <=> It will not have to encounter holes on its way downward.
     */
    bool can_use_safe_radius : 1;

    /*!
     * \brief Skip the ovalisation to parent and children when generating the final circles.
     */
    bool skip_ovalisation : 1;

#ifdef TREE_SUPPORTS_TRACK_LOST
    // Likely a lost branch, debugging information.
    bool lost : 1;
    bool verylost : 1;
#endif // TREE_SUPPORTS_TRACK_LOST

    // Not valid anymore, to be deleted.
    bool deleted : 1;

    // General purpose flag marking a visited element.
    bool marked : 1;
};

struct SupportElementState : public SupportElementStateBits
{
    /*!
     * \brief The layer this support elements wants reach
     */
    LayerIndex target_height;

    /*!
     * \brief The position this support elements wants to support on layer=target_height
     */
    Point target_position;

    /*!
     * \brief The next position this support elements wants to reach. NOTE: This is mainly a suggestion regarding direction inside the influence area.
     */
    Point next_position;

    /*!
     * \brief The next height this support elements wants to reach
     */
    LayerIndex layer_idx;

    /*!
     * \brief The Effective distance to top of this element regarding radius increases and collision calculations.
     */
    uint32_t effective_radius_height;

    /*!
     * \brief The amount of layers this element is below the topmost layer of this branch.
     */
    uint32_t distance_to_top;

    /*!
     * \brief The resulting center point around which a circle will be drawn later.
     * Will be set by setPointsOnAreas
     */
    Point result_on_layer{std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max()};
    bool result_on_layer_is_set() const
    {
        return this->result_on_layer != Point{std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max()};
    }
    void result_on_layer_reset()
    {
        this->result_on_layer = Point{std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max()};
    }
    /*!
     * \brief The amount of extra radius we got from merging branches that could have reached the buildplate, but merged with ones that can not.
     */
    coord_t increased_to_model_radius; // how much to model we increased only relevant for merging

    /*!
     * \brief Counter about the times the elephant foot was increased. Can be fractions for merge reasons.
     */
    double elephant_foot_increases;

    // Foot radius a planted Baobab trunk was fitted to when its landing was narrower than the
    // trunk, 0 when the trunk stands at full radius. The drawn branch tapers down to it over its
    // lowest layers at the support overhang angle.
    coord_t plant_foot_radius{0};

    /*!
     * \brief The element tries to not move until this dtt is reached, is set to 0 if the element had to move.
     */
    uint32_t dont_move_until;

    /*!
     * \brief Settings used to increase the influence area to its current state.
     */
    AreaIncreaseSettings last_area_increase;

    /*!
     * \brief Amount of roof layers that were not yet added, because the branch needed to move.
     */
    uint32_t missing_roof_layers;

    // called by increase_single_area() and increaseAreas()
    [[nodiscard]] static SupportElementState propagate_down(const SupportElementState &src)
    {
        SupportElementState dst{src};
        ++dst.distance_to_top;
        --dst.layer_idx;
        // set to invalid as we are a new node on a new layer
        dst.result_on_layer_reset();
        dst.skip_ovalisation = false;
        return dst;
    }

    [[nodiscard]] bool locked() const { return this->distance_to_top < this->dont_move_until; }
};

/*!
 * \brief Get the Distance to top regarding the real radius this part will have. This is different from distance_to_top, which is can be used to calculate the top most layer of the branch.
 * \param elem[in] The SupportElement one wants to know the effectiveDTT
 * \return The Effective DTT.
 */
[[nodiscard]] inline size_t getEffectiveDTT(const TreeSupportSettings &settings, const SupportElementState &elem)
{
    return elem.effective_radius_height < settings.increase_radius_until_layer
               ? (elem.distance_to_top < settings.increase_radius_until_layer ? elem.distance_to_top
                                                                              : settings.increase_radius_until_layer)
               : elem.effective_radius_height;
}

/*!
 * \brief Get the Radius, that this element will have.
 * \param elem[in] The Element.
 * \return The radius the element has.
 */
[[nodiscard]] inline coord_t support_element_radius(const TreeSupportSettings &settings,
                                                    const SupportElementState &elem)
{
    return settings.getRadius(getEffectiveDTT(settings, elem), elem.elephant_foot_increases);
}

/*!
 * \brief Get the collision Radius of this Element. This can be smaller then the actual radius, as the drawAreas will cut off areas that may collide with the model.
 * \param elem[in] The Element.
 * \return The collision radius the element has.
 */
[[nodiscard]] inline coord_t support_element_collision_radius(const TreeSupportSettings &settings,
                                                              const SupportElementState &elem)
{
    return settings.getRadius(elem.effective_radius_height, elem.elephant_foot_increases);
}

struct SupportElement
{
    using ParentIndices =
#ifdef NDEBUG
        // To reduce memory allocation in release mode.
        boost::container::small_vector<int32_t, 4>;
#else  // NDEBUG
       // To ease debugging.
        std::vector<int32_t>;
#endif // NDEBUG

    //    SupportElement(const SupportElementState &state) : SupportElementState(state) {}
    SupportElement(const SupportElementState &state, Polygons &&influence_area)
        : state(state), influence_area(std::move(influence_area))
    {
    }
    SupportElement(const SupportElementState &state, ParentIndices &&parents, Polygons &&influence_area)
        : state(state), parents(std::move(parents)), influence_area(std::move(influence_area))
    {
    }

    SupportElementState state;

    /*!
     * \brief All elements in the layer above the current one that are supported by this element
     */
    ParentIndices parents;

    /*!
     * \brief The resulting influence area.
     * Will only be set in the results of createLayerPathing, and will be nullptr inside!
     */
    Polygons influence_area;
};

using SupportElements = std::deque<SupportElement>;

[[nodiscard]] inline coord_t support_element_radius(const TreeSupportSettings &settings, const SupportElement &elem)
{
    return support_element_radius(settings, elem.state);
}

[[nodiscard]] inline coord_t support_element_collision_radius(const TreeSupportSettings &settings,
                                                              const SupportElement &elem)
{
    return support_element_collision_radius(settings, elem.state);
}

// Determinism fingerprint of the element tree: one line per layer holding elements, the state
// of every element (positions, radii, counters, flags), its parent count and its influence area,
// summed over the layer so element order does not matter.
inline void determ_fp_move_bounds(const char *tag, const SlicingParameters &slicing_params,
                                  const TreeSupportSettings &config, const std::vector<SupportElements> &move_bounds)
{
    if (!debug_enabled(DBG_SUPPORT))
        return;
    for (size_t layer_idx = 0; layer_idx < move_bounds.size(); ++layer_idx)
    {
        const SupportElements &layer = move_bounds[layer_idx];
        if (layer.empty())
            continue;
        DetermFingerprint fp;
        size_t parents = 0, placed = 0;
        for (const SupportElement &el : layer)
        {
            const SupportElementState &s = el.state;
            uint64_t h = 1469598103934665603ull;
            h = DetermFingerprint::mix(h, uint64_t(s.target_height));
            h = DetermFingerprint::mix(h, s.target_position);
            h = DetermFingerprint::mix(h, s.next_position);
            h = DetermFingerprint::mix(h, uint64_t(s.layer_idx));
            h = DetermFingerprint::mix(h, uint64_t(s.effective_radius_height));
            h = DetermFingerprint::mix(h, uint64_t(s.distance_to_top));
            h = DetermFingerprint::mix(h, s.result_on_layer);
            h = DetermFingerprint::mix(h, uint64_t(s.increased_to_model_radius));
            h = DetermFingerprint::mix(h, uint64_t(std::llround(s.elephant_foot_increases * 1e6)));
            h = DetermFingerprint::mix(h, uint64_t(s.plant_foot_radius));
            h = DetermFingerprint::mix(h, uint64_t(s.dont_move_until));
            h = DetermFingerprint::mix(h, uint64_t(s.missing_roof_layers));
            h = DetermFingerprint::mix(h, uint64_t(s.to_buildplate) | uint64_t(s.to_model_gracious) << 1 |
                                              uint64_t(s.use_min_xy_dist) << 2 | uint64_t(s.supports_roof) << 3 |
                                              uint64_t(s.can_use_safe_radius) << 4 | uint64_t(s.skip_ovalisation) << 5 |
                                              uint64_t(s.deleted) << 6);
            h = DetermFingerprint::mix(h, uint64_t(el.parents.size()));
            h = DetermFingerprint::mix(h, DetermFingerprint::polygons(el.influence_area));
            fp.add(h);
            parents += el.parents.size();
            placed += s.result_on_layer_is_set();
            for (const Polygon &poly : el.influence_area)
            {
                fp.points += poly.points.size();
                fp.area += poly.area();
                for (const Point &pt : poly.points)
                    fp.ordered = DetermFingerprint::mix(fp.ordered, pt);
            }
        }
        dbg_log(DBG_SUPPORT, layer_z(slicing_params, config, layer_idx), "DETERM",
                "%s layer=%zu elems=%zu parents=%zu placed=%zu pts=%zu area=%.4fmm2 hash=%016llx ord=%016llx", tag,
                layer_idx, fp.count, parents, placed, fp.points, fp.area_mm2(), (unsigned long long) fp.hash,
                (unsigned long long) fp.ordered);
    }
}

} // namespace FFFTreeSupport

void fff_tree_support_generate(
    PrintObject &print_object, std::function<void()> throw_on_cancel = [] {},
    const std::vector<Polygons> &additional_excluded_areas = {});

} // namespace Luminary
