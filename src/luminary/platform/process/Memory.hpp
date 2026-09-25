///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Sijmen Schoon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <stddef.h>
#include <string>

namespace Luminary
{

// Format memory allocated, separate thousands by comma.
extern std::string format_memsize_MB(size_t n);
extern std::string format_memsize(size_t bytes, unsigned int decimals = 1);
// Return string to be added to the boost::log output to inform about the current process memory allocation.
// The string is non-empty if the loglevel >= info (3) or ignore_loglevel==true.
// Latter is used to get the memory info from SysInfoDialog.
extern std::string log_memory_info(bool ignore_loglevel = false);
// Returns the size of physical memory (RAM) in bytes.
extern size_t total_physical_memory();

} // namespace Luminary

#if WIN32
#define PREFLIGHT_STDVEC_MEMSIZE(NAME, TYPE) \
    NAME.capacity() * ((sizeof(TYPE) + __alignof(TYPE) - 1) / __alignof(TYPE)) * __alignof(TYPE)
// Estimate only; the bucket array and the per node links are not counted.
#define PREFLIGHT_STDUNORDEREDSET_MEMSIZE(NAME, TYPE) \
    NAME.size() * ((sizeof(TYPE) + __alignof(TYPE) - 1) / __alignof(TYPE)) * __alignof(TYPE)
#else
#define PREFLIGHT_STDVEC_MEMSIZE(NAME, TYPE) \
    NAME.capacity() * ((sizeof(TYPE) + alignof(TYPE) - 1) / alignof(TYPE)) * alignof(TYPE)
// Estimate only; the bucket array and the per node links are not counted.
#define PREFLIGHT_STDUNORDEREDSET_MEMSIZE(NAME, TYPE) \
    NAME.size() * ((sizeof(TYPE) + alignof(TYPE) - 1) / alignof(TYPE)) * alignof(TYPE)
#endif
