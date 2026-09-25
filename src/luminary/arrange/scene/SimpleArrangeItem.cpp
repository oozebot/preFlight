///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <luminary/arrange/scene/SimpleArrangeItem.hpp>
#include "luminary/arrange/ArrangeImpl.hpp" // IWYU pragma: keep
#include "ArrangeTaskImpl.hpp"              // IWYU pragma: keep
#include "FillBedTaskImpl.hpp"              // IWYU pragma: keep
#include "MultiplySelectionTaskImpl.hpp"    // IWYU pragma: keep

namespace Luminary
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
} // namespace Luminary
