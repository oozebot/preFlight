///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef VGCODE_EXTRUSION_ROLES_HPP
#define VGCODE_EXTRUSION_ROLES_HPP

#include "../include/Types.hpp"

#include <map>

namespace libvgcode
{

class ExtrusionRoles
{
public:
    struct Item
    {
        std::array<float, TIME_MODES_COUNT> times;
    };

    void add(EGCodeExtrusionRole role, const std::array<float, TIME_MODES_COUNT> &times);

    std::size_t get_roles_count() const { return m_items.size(); }
    std::vector<EGCodeExtrusionRole> get_roles() const;
    float get_time(EGCodeExtrusionRole role, ETimeMode mode) const;

    void reset() { m_items.clear(); }

private:
    std::map<EGCodeExtrusionRole, Item> m_items;
};

} // namespace libvgcode

#endif // VGCODE_EXTRUSION_ROLES_HPP
