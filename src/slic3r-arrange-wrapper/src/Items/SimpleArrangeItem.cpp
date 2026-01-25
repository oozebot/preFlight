///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <arrange-wrapper/Items/SimpleArrangeItem.hpp>
#include "ArrangeImpl.hpp"                     // IWYU pragma: keep
#include "Tasks/ArrangeTaskImpl.hpp"           // IWYU pragma: keep
#include "Tasks/FillBedTaskImpl.hpp"           // IWYU pragma: keep
#include "Tasks/MultiplySelectionTaskImpl.hpp" // IWYU pragma: keep

namespace Slic3r
{
namespace arr2
{

Polygon SimpleArrangeItem::outline() const
{
    Polygon ret = shape();
    ret.rotate(m_rotation);
    ret.translate(m_translation);

    return ret;
}

template class ArrangeableToItemConverter<SimpleArrangeItem>;
template struct ArrangeTask<SimpleArrangeItem>;
template struct FillBedTask<SimpleArrangeItem>;
template struct MultiplySelectionTask<SimpleArrangeItem>;
template class Arranger<SimpleArrangeItem>;

} // namespace arr2
} // namespace Slic3r
