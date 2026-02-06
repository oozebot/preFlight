///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_NotesDialog_hpp_
#define slic3r_NotesDialog_hpp_

#include <string>
#include <vector>

namespace Slic3r {

class Model;
class ModelObject;

namespace GUI {

class NotesDialog
{
public:
    NotesDialog();
    ~NotesDialog();

    // Show the dialog, optionally pre-selecting an object
    // -1 = "All objects" (project notes)
    void show(int preselect_object_idx = -1);
    void hide();
    void toggle();
    bool is_visible() const { return m_visible; }

    // Called from render loop
    void render();

    // Called when selection changes on platter
    void on_selection_changed(int object_idx);

    // Called when objects are added/removed/renamed
    void on_objects_changed();

private:
    void render_object_list();
    void render_notes_editor();
    void save_current_notes();

    bool m_visible{false};
    int m_selected_idx{-1}; // -1 = "All objects" (project notes)
    std::string m_edit_buffer;
    bool m_read_only{false}; // True when in Preview tab
    bool m_needs_save{false};

    // Cached object names for display
    std::vector<std::string> m_object_names;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_NotesDialog_hpp_
