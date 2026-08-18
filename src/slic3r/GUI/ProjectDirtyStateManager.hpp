///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2021 - 2022 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_ProjectDirtyStateManager_hpp_
#define slic3r_ProjectDirtyStateManager_hpp_

#include "libslic3r/Preset.hpp"
#include "libslic3r/CustomGCode.hpp"

namespace Slic3r
{
namespace GUI
{

class ProjectDirtyStateManager
{
public:
    void update_from_undo_redo_stack(bool dirty);
    void update_from_presets();
    void update_from_preview();
    void reset_after_save();
    void reset_initial_presets();
    // Mark the project dirty for an edit the recomputing updaters cannot see, e.g. a filament
    // color committed into a preset baseline. Cleared by reset_after_save / reset_initial_presets.
    void set_forced_dirty();

    bool is_dirty() const
    {
        return m_plater_dirty || m_project_config_dirty || m_presets_dirty || m_custom_gcode_per_print_z_dirty ||
               m_forced_dirty;
    }
    bool is_presets_dirty() const { return m_presets_dirty; }

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_debug_window() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

private:
    // Does the Undo / Redo stack indicate the project is dirty?
    bool m_plater_dirty{false};
    // Do the presets indicate the project is dirty?
    bool m_presets_dirty{false};
    // Is the project config dirty?
    bool m_project_config_dirty{false};
    // Is the custom_gcode_per_print_z dirty?
    bool m_custom_gcode_per_print_z_dirty{false};
    // Dirty for an edit the recomputing updaters cannot detect (e.g. a preset-baseline write)
    bool m_forced_dirty{false};
    // Keeps track of preset names selected at the time of last project save.
    std::array<std::string, Preset::TYPE_COUNT> m_initial_presets;
    DynamicPrintConfig m_initial_project_config;
    CustomGCode::Info m_initial_custom_gcode_per_print_z;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ProjectDirtyStateManager_hpp_
