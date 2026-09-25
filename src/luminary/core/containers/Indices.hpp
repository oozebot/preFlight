///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

namespace Luminary
{

template<typename INDEX_TYPE>
inline INDEX_TYPE prev_idx_modulo(INDEX_TYPE idx, const INDEX_TYPE count)
{
    if (idx == 0)
        idx = count;
    return --idx;
}

template<typename INDEX_TYPE>
inline INDEX_TYPE next_idx_modulo(INDEX_TYPE idx, const INDEX_TYPE count)
{
    if (++idx == count)
        idx = 0;
    return idx;
}

// Return dividend divided by divisor rounded to the nearest integer
template<typename INDEX_TYPE>
inline INDEX_TYPE round_up_divide(const INDEX_TYPE dividend, const INDEX_TYPE divisor)
{
    return (dividend + divisor - 1) / divisor;
}

template<typename CONTAINER_TYPE>
inline typename CONTAINER_TYPE::size_type prev_idx_modulo(typename CONTAINER_TYPE::size_type idx,
                                                          const CONTAINER_TYPE &container)
{
    return prev_idx_modulo(idx, container.size());
}

template<typename CONTAINER_TYPE>
inline typename CONTAINER_TYPE::size_type next_idx_modulo(typename CONTAINER_TYPE::size_type idx,
                                                          const CONTAINER_TYPE &container)
{
    return next_idx_modulo(idx, container.size());
}

template<typename CONTAINER_TYPE>
inline const typename CONTAINER_TYPE::value_type &prev_value_modulo(typename CONTAINER_TYPE::size_type idx,
                                                                    const CONTAINER_TYPE &container)
{
    return container[prev_idx_modulo(idx, container.size())];
}

template<typename CONTAINER_TYPE>
inline typename CONTAINER_TYPE::value_type &prev_value_modulo(typename CONTAINER_TYPE::size_type idx,
                                                              CONTAINER_TYPE &container)
{
    return container[prev_idx_modulo(idx, container.size())];
}

template<typename CONTAINER_TYPE>
inline const typename CONTAINER_TYPE::value_type &next_value_modulo(typename CONTAINER_TYPE::size_type idx,
                                                                    const CONTAINER_TYPE &container)
{
    return container[next_idx_modulo(idx, container.size())];
}

template<typename CONTAINER_TYPE>
inline typename CONTAINER_TYPE::value_type &next_value_modulo(typename CONTAINER_TYPE::size_type idx,
                                                              CONTAINER_TYPE &container)
{
    return container[next_idx_modulo(idx, container.size())];
}

} // namespace Luminary
