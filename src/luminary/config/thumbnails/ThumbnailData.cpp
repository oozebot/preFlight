///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2020 Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
///|/ Provisional module: thumbnails belong with gcode by subject, and sit in config because the option catalogue includes this header.
///|/
#include "ThumbnailData.hpp"

namespace Luminary
{

void ThumbnailData::set(unsigned int w, unsigned int h)
{
    if ((w == 0) || (h == 0))
        return;

    if ((width != w) || (height != h))
    {
        width = w;
        height = h;
        // defaults to white texture
        pixels = std::vector<unsigned char>(width * height * 4, 255);
    }
}

void ThumbnailData::reset()
{
    width = 0;
    height = 0;
    pixels.clear();
}

bool ThumbnailData::is_valid() const
{
    return (width != 0) && (height != 0) && ((unsigned int) pixels.size() == 4 * width * height);
}

} // namespace Luminary
