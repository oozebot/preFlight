///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "GLGizmoBase.hpp"
#include "DSKY/GUI/GLModel.hpp"

#include <optional>

namespace DSKY
{
using namespace Luminary;

class GLGizmoRelief : public GLGizmoBase
{
public:
    GLGizmoRelief(GLCanvas3D &parent);

    // Called from the menu to immediately open the file dialog on next render
    void request_load_image() { m_load_requested = true; }

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    bool on_is_selectable() const override { return false; } // No toolbar icon
    void on_render() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    void on_set_state() override;
    void on_register_raycasters_for_picking() override {}
    void on_unregister_raycasters_for_picking() override {}

private:
    // Image data (grayscale, 16-bit)
    struct ImageData
    {
        std::vector<uint16_t> pixels; // Row-major, 16-bit grayscale
        unsigned int width{0};
        unsigned int height{0};
        unsigned int bit_depth{8}; // Original bit depth (8 or 16)
        std::string filename;
    };

    std::optional<ImageData> m_image;

    // User-adjustable parameters
    float m_width_mm{50.f};   // Physical width on platter
    float m_height_mm{50.f};  // Physical height (derived from aspect ratio)
    float m_depth_mm{5.f};    // Max depth (white pixel height)
    bool m_lock_aspect{true}; // Lock width/height aspect ratio
    float m_aspect_ratio{1.f};
    bool m_inverted{false};      // Invert relief: swap raised/recessed without modifying pixel data
    float m_smoothing{2.f};      // Gaussian blur radius in pixels (0 = no smoothing)
    float m_gamma{1.f};          // Gamma correction: <1 brightens, >1 darkens
    float m_min_thickness{0.f};  // Minimum thickness at the thinnest point (0 = cutout)
    bool m_solid_base{false};    // When true, black pixels get a solid base instead of holes
    unsigned int m_max_dim{500}; // Max pixels per dimension for mesh generation

    // Blurred pixel buffer - regenerated when smoothing or image changes
    std::vector<uint16_t> m_blurred_pixels;

    // Preview mesh rendered while gizmo is open (built at full detail)
    GLModel m_preview;
    indexed_triangle_set m_preview_its; // Cached for reuse by generate_mesh
    bool m_preview_dirty{true};
    bool m_load_requested{false};
    bool m_objects_hidden{false};

    // Open a file dialog and load the selected image
    bool load_image();

    // Rebuild the Gaussian-blurred pixel buffer from m_image
    void rebuild_blurred_pixels();

    // Build the indexed_triangle_set from the image at a given max resolution
    indexed_triangle_set build_heightmap_its(unsigned int max_dim) const;

    // Rebuild the low-res preview GLModel
    void update_preview();

    // Generate the heightmap mesh and add it to the platter
    void generate_mesh();
};

} // namespace DSKY

