///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <optional>

// The worker-count policy: the cap the CLI and the preferences set, and the call that applies it to
// the TBB thread pool. Empty means all the hardware threads. The definition of enforce_thread_count
// is in Thread.cpp, beside the thread_local worker state it sizes; these two declarations are a
// header of their own because Thread.hpp's own API needs boost/thread.hpp, and on Windows that
// reaches windows.h, which the callers of this policy must not be made to pay for.
namespace Luminary
{

inline std::optional<std::size_t> thread_count;
extern void enforce_thread_count(std::size_t count);

} // namespace Luminary
