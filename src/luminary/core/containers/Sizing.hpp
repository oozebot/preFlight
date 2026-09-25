///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Luminary
{

// Compute the next highest power of 2 of 32-bit v
// http://graphics.stanford.edu/~seander/bithacks.html
inline uint16_t next_highest_power_of_2(uint16_t v)
{
    if (v != 0)
        --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    return ++v;
}
inline uint32_t next_highest_power_of_2(uint32_t v)
{
    if (v != 0)
        --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++v;
}
inline uint64_t next_highest_power_of_2(uint64_t v)
{
    if (v != 0)
        --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return ++v;
}

// On some implementations (such as some versions of clang), the size_t is a type of its own, so we need to overload for size_t.
// Typically, though, the size_t type aliases to uint64_t / uint32_t.
// We distinguish that here and provide implementation for size_t if and only if it is a distinct type
template<class T>
size_t next_highest_power_of_2(
    T v, typename std::enable_if<std::is_same<T, size_t>::value, T>::type = 0, // T is size_t
    typename std::enable_if<!std::is_same<T, uint64_t>::value, T>::type = 0,   // T is not uint64_t
    typename std::enable_if<!std::is_same<T, uint32_t>::value, T>::type = 0,   // T is not uint32_t
    typename std::enable_if<sizeof(T) == 8, T>::type = 0)                      // T is 64 bits
{
    return next_highest_power_of_2(uint64_t(v));
}
template<class T>
size_t next_highest_power_of_2(
    T v, typename std::enable_if<std::is_same<T, size_t>::value, T>::type = 0, // T is size_t
    typename std::enable_if<!std::is_same<T, uint64_t>::value, T>::type = 0,   // T is not uint64_t
    typename std::enable_if<!std::is_same<T, uint32_t>::value, T>::type = 0,   // T is not uint32_t
    typename std::enable_if<sizeof(T) == 4, T>::type = 0)                      // T is 32 bits
{
    return next_highest_power_of_2(uint32_t(v));
}

template<class VectorType>
void reserve_power_of_2(VectorType &vector, size_t n)
{
    vector.reserve(next_highest_power_of_2(n));
}

template<class VectorType>
void reserve_more(VectorType &vector, size_t n)
{
    vector.reserve(vector.size() + n);
}

template<class VectorType>
void reserve_more_power_of_2(VectorType &vector, size_t n)
{
    vector.reserve(next_highest_power_of_2(vector.size() + n));
}

} // namespace Luminary
