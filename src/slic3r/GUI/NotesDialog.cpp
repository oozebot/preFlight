///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "NotesDialog.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"

#include "libslic3r/Model.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_stdlib.h> // For std::string input support

namespace Slic3r
{
namespace GUI
{

NotesDialog::NotesDialog() = default;
NotesDialog::~NotesDialog() = default;

void NotesDialog::show(int preselect_object_idx)
{
    m_visible = true;
    m_selected_idx = preselect_object_idx;
    on_objects_changed(); // Refresh object list and load notes
}

void NotesDialog::hide()
{
    save_current_notes();
    m_visible = false;
}

void NotesDialog::toggle()
{
    if (m_visible)
        hide();
    else
        show();
}

void NotesDialog::on_selection_changed(int object_idx)
{
    if (m_visible && object_idx >= 0)
    {
        // Save current notes before switching
        save_current_notes();
        m_selected_idx = object_idx;
        // Load the notes for the newly selected object
        on_objects_changed();
    }
}

void NotesDialog::on_objects_changed()
{
    m_object_names.clear();

    Plater *plater = wxGetApp().plater();
    if (!plater)
        return;

    const Model &model = plater->model();
    for (const ModelObject *obj : model.objects)
        m_object_names.push_back(obj->name);

    // Validate selected index
    if (m_selected_idx >= (int) m_object_names.size())
        m_selected_idx = -1;

    // Load the notes for current selection into the edit buffer
    if (m_selected_idx == -1)
    {
        m_edit_buffer = model.project_notes;
    }
    else if (m_selected_idx >= 0 && m_selected_idx < (int) model.objects.size())
    {
        m_edit_buffer = model.objects[m_selected_idx]->notes;
    }
    m_needs_save = false;
}

void NotesDialog::save_current_notes()
{
    if (!m_needs_save)
        return;

    Plater *plater = wxGetApp().plater();
    if (!plater)
        return;

    Model &model = plater->model();

    // Take undo snapshot before saving
    plater->take_snapshot(_L("Edit Notes"));

    if (m_selected_idx == -1)
    {
        model.project_notes = m_edit_buffer;
    }
    else if (m_selected_idx >= 0 && m_selected_idx < (int) model.objects.size())
    {
        model.objects[m_selected_idx]->notes = m_edit_buffer;
    }

    m_needs_save = false;
}

void NotesDialog::render()
{
    if (!m_visible)
        return;

    Plater *plater = wxGetApp().plater();
    if (!plater)
        return;

    // Check if in preview mode (read-only)
    m_read_only = plater->is_preview_shown();

    // Window flags
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    // preFlight: hide resize grip (window is still resizable by dragging edges)
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, ImVec4(0, 0, 0, 0));

    // Set minimum size constraints
    ImGui::SetNextWindowSizeConstraints(ImVec2(600, 200), ImVec2(FLT_MAX, FLT_MAX));

    // Set initial size (only on first open)
    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);

    bool is_open = true;
    std::string title = _u8L("Project notes");
    if (m_read_only)
        title += " " + _u8L("(Read only)");
    title += "###NotesDialog"; // Unique ID for window

    if (ImGui::Begin(title.c_str(), &is_open, flags))
    {
        // Two-column layout
        // DPI-scaled list width (15 * font_size instead of fixed 150px)
        float list_width = ImGui::GetFontSize() * 15.0f;

        // Left panel - object list
        ImGui::BeginChild("ObjectList", ImVec2(list_width, 0), true);
        render_object_list();
        ImGui::EndChild();

        ImGui::SameLine();

        // Right panel - notes editor
        ImGui::BeginChild("NotesEditor", ImVec2(0, 0), true);
        render_notes_editor();
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(3); // ResizeGrip colors

    if (!is_open)
        hide();
}

void NotesDialog::render_object_list()
{
    // "All Objects" entry for project notes
    bool is_selected = (m_selected_idx == -1);
    if (ImGui::Selectable(_u8L("All objects").c_str(), is_selected))
    {
        if (m_selected_idx != -1)
        {
            save_current_notes();
            m_selected_idx = -1;

            // Load project notes
            Plater *plater = wxGetApp().plater();
            if (plater)
            {
                m_edit_buffer = plater->model().project_notes;
                m_needs_save = false;
            }
        }
    }

    ImGui::Separator();

    // Object entries
    for (size_t i = 0; i < m_object_names.size(); ++i)
    {
        is_selected = (m_selected_idx == (int) i);
        std::string label = m_object_names[i];
        if (label.empty())
            label = _u8L("Unnamed object") + " " + std::to_string(i + 1);

        // Add unique ID to handle duplicate names
        label += "###obj_" + std::to_string(i);

        if (ImGui::Selectable(label.c_str(), is_selected))
        {
            if (m_selected_idx != (int) i)
            {
                save_current_notes();
                m_selected_idx = (int) i;

                // Load object notes
                Plater *plater = wxGetApp().plater();
                if (plater && m_selected_idx < (int) plater->model().objects.size())
                {
                    m_edit_buffer = plater->model().objects[m_selected_idx]->notes;
                    m_needs_save = false;
                }
            }
        }
    }
}

void NotesDialog::render_notes_editor()
{
    Plater *plater = wxGetApp().plater();
    if (!plater)
        return;

    // Header
    std::string header;
    if (m_selected_idx == -1)
        header = _u8L("Project notes");
    else if (m_selected_idx < (int) m_object_names.size())
    {
        header = m_object_names[m_selected_idx];
        if (header.empty())
            header = _u8L("Unnamed object") + " " + std::to_string(m_selected_idx + 1);
    }

    ImGui::Text("%s", header.c_str());
    ImGui::Separator();

    // Text editor flags
    ImGuiInputTextFlags text_flags = ImGuiInputTextFlags_AllowTabInput;
    if (m_read_only)
        text_flags |= ImGuiInputTextFlags_ReadOnly;

    // Use available space for the text area
    ImVec2 text_size = ImGui::GetContentRegionAvail();
    if (m_read_only)
        text_size.y -= ImGui::GetTextLineHeightWithSpacing(); // Leave space for status

    // Use a unique ID based on selection so ImGui resets its internal buffer when switching
    std::string input_id = "##Notes_" + std::to_string(m_selected_idx);
    if (ImGui::InputTextMultiline(input_id.c_str(), &m_edit_buffer, text_size, text_flags))
    {
        if (!m_read_only)
            m_needs_save = true;
    }

    // Auto-save when focus is lost from the text area
    if (m_needs_save && !ImGui::IsItemActive())
    {
        save_current_notes();
    }

    // Status line for read-only mode
    if (m_read_only)
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", _u8L("Switch to Prepare tab to edit").c_str());
}

} // namespace GUI
} // namespace Slic3r
