///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <vector>
#include <luminary/geometry/contours/Polygon.hpp>
#include <luminary/geometry/primitives/Point.hpp>
#include <luminary/gizmo/emboss/Emboss.hpp>
#include "DSKY/GUI/GLModel.hpp"
#include "DSKY/Utils/EmbossStyleManager.hpp"

namespace Luminary
{
class ModelVolume;
typedef std::vector<ModelVolume *> ModelVolumePtrs;
struct FontProp;
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

class TextLinesModel
{
public:
    /// <summary>
    /// Initialize model and lines
    /// </summary>
    /// <param name="text_tr">Transformation of text volume inside object (aka inside of instance)</param>
    /// <param name="volumes_to_slice">Vector of volumes to be sliced</param>
    /// <param name="style_manager">Contain Font file, size and align</param>
    /// <param name="count_lines">Count lines of embossed text(for veritcal alignment)</param>
    void init(const Transform3d &text_tr, const ModelVolumePtrs &volumes_to_slice,
              /*const*/ Emboss::StyleManager &style_manager, unsigned count_lines);

    void render(const Transform3d &text_world);

    bool is_init() const { return m_model.is_initialized(); }
    void reset()
    {
        m_model.reset();
        m_lines.clear();
    }
    const Luminary::Emboss::TextLines &get_lines() const { return m_lines; }

private:
    Luminary::Emboss::TextLines m_lines;

    // Keep model for visualization text lines
    GLModel m_model;
};
} // namespace DSKY

namespace Luminary::Emboss
{
/// <summary>
/// creation line without model for backend only
/// </summary>
/// <param name="text_tr">Transformation of text volume inside object (aka inside of
/// instance)</param> <param name="volumes_to_slice">Vector of volumes to be sliced</param> <param
/// name="ff"></param> <param name="fp"></param> <param name="count_lines">Count lines of embossed
/// text(for veritcal alignment)</param> <param name="line_height_mm_ptr">[output] line height in
/// mm</param> <returns></returns>
TextLines create_text_lines(const Transform3d &text_tr, const ModelVolumePtrs &volumes_to_slice, const FontFile &ff,
                            const FontProp &fp, unsigned count_lines = 1, double *line_height_mm_ptr = nullptr);
} // namespace Luminary::Emboss
