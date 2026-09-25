///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Tomáš Mészáros @tamasmeszaros, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

// The types Print.hpp shares with the fill and G-code modules without including their headers:
// the deleters PrintObject holds by value for its opaque octrees and lightning generator, and the
// sequential-print conflict record the G-code processor fills in.

#include <memory>
#include <optional>
#include <string>

namespace Luminary
{

namespace FillAdaptive
{

struct Octree;

// To keep the definition of Octree opaque, we have to define a custom deleter.
struct OctreeDeleter
{
    void operator()(Octree *p);
};
using OctreePtr = std::unique_ptr<Octree, OctreeDeleter>;

} // namespace FillAdaptive

namespace FillLightning
{

class Generator;

// To keep the definition of Octree opaque, we have to define a custom deleter.
struct GeneratorDeleter
{
    void operator()(Generator *p);
};
using GeneratorPtr = std::unique_ptr<Generator, GeneratorDeleter>;

} // namespace FillLightning

struct ConflictResult
{
    std::string _objName1;
    std::string _objName2;
    double _height;
    const void *_obj1; // nullptr means wipe tower
    const void *_obj2;
    int layer = -1;
    ConflictResult(const std::string &objName1, const std::string &objName2, double height, const void *obj1,
                   const void *obj2)
        : _objName1(objName1), _objName2(objName2), _height(height), _obj1(obj1), _obj2(obj2)
    {
    }
    ConflictResult() = default;
};

using ConflictResultOpt = std::optional<ConflictResult>;

} // namespace Luminary
