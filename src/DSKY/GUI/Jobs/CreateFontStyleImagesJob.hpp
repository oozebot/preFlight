///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Filip Sykala @Jony01
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <vector>
#include <string>
#include <luminary/gizmo/emboss/Emboss.hpp>
#include "DSKY/Utils/EmbossStyleManager.hpp"
#include "Job.hpp"

namespace DSKY
{
using namespace Luminary;
}
namespace DSKY::Emboss
{

/// <summary>
/// Create texture with name of styles written by its style
/// Access to glyph cache is possible only from job
/// </summary>
class CreateFontStyleImagesJob : public Job
{
    StyleManager::StyleImagesData m_input;

    // Output data
    // texture size
    int m_width, m_height;
    // texture data
    std::vector<unsigned char> m_pixels;
    // descriptors of sub textures
    std::vector<StyleManager::StyleImage> m_images;

public:
    CreateFontStyleImagesJob(StyleManager::StyleImagesData &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &) override;
};

} // namespace DSKY::Emboss
