///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2021 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_ThumbnailData_hpp_
#define slic3r_ThumbnailData_hpp_

#include <vector>
#include <functional>

#include "libslic3r/Point.hpp"

namespace Slic3r
{

struct ThumbnailData
{
    unsigned int width;
    unsigned int height;
    std::vector<unsigned char> pixels;

    ThumbnailData() { reset(); }
    void set(unsigned int w, unsigned int h);
    void reset();

    bool is_valid() const;
};

using ThumbnailsList = std::vector<ThumbnailData>;

struct ThumbnailsParams
{
    Vec2ds sizes;
    bool printable_only;
    bool parts_only;
    bool show_bed;
    bool transparent_background;
};

typedef std::function<ThumbnailsList(const ThumbnailsParams &)> ThumbnailsGeneratorCallback;

} // namespace Slic3r

#endif // slic3r_ThumbnailData_hpp_
