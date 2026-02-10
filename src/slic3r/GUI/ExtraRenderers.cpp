///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2022 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ExtraRenderers.hpp"
#include "wxExtensions.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "BitmapComboBox.hpp"
#include "Plater.hpp"

#include <wx/dc.h>

using Slic3r::GUI::from_u8;
using Slic3r::GUI::into_u8;

//-----------------------------------------------------------------------------
// DataViewBitmapText
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(DataViewBitmapText, wxObject)

    IMPLEMENT_VARIANT_OBJECT(DataViewBitmapText)

        static wxSize get_size(const wxBitmap &icon)
{
#ifdef __WIN32__
    return icon.GetSize();
#else
    return icon.GetScaledSize();
#endif
}

// ---------------------------------------------------------
// BitmapTextRenderer
// ---------------------------------------------------------

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
BitmapTextRenderer::BitmapTextRenderer(wxDataViewCellMode mode /*= wxDATAVIEW_CELL_EDITABLE*/,
                                       int align /*= wxDVR_DEFAULT_ALIGNMENT*/)
    : wxDataViewRenderer(wxT("preFlightDataViewBitmapText"), mode, align)
{
    SetMode(mode);
    SetAlignment(align);
}
#endif // ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

// Destructor no longer needs to delete m_markupText (upstream wx fork feature)
BitmapTextRenderer::~BitmapTextRenderer() {}

void BitmapTextRenderer::EnableMarkup(bool /*enable*/)
{
    // Markup support disabled - upstream wx fork feature not available in stock wx
}

bool BitmapTextRenderer::SetValue(const wxVariant &value)
{
    m_value << value;
    return true;
}

bool BitmapTextRenderer::GetValue(wxVariant &WXUNUSED(value)) const
{
    return false;
}

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING && wxUSE_ACCESSIBILITY
wxString BitmapTextRenderer::GetAccessibleDescription() const
{
    return m_value.GetText();
}
#endif // wxUSE_ACCESSIBILITY && ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

bool BitmapTextRenderer::Render(wxRect rect, wxDC *dc, int state)
{
    int xoffset = 0;

    const wxBitmap &icon = m_value.GetBitmap();
    if (icon.IsOk())
    {
        wxSize icon_sz = get_size(icon);
        dc->DrawBitmap(icon, rect.x, rect.y + (rect.height - icon_sz.y) / 2);
        xoffset = icon_sz.x + 4;
    }

    // Set text color based on current theme palette (cross-platform)
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour text_color = is_dark ? wxColour(250, 250, 250) : wxColour(38, 46, 48);

    // For selected items, use appropriate contrast color
    if (state & wxDATAVIEW_CELL_SELECTED)
        text_color = is_dark ? wxColour(255, 255, 255) : wxColour(38, 46, 48);

    dc->SetTextForeground(text_color);

#ifndef _WIN32
    {
        wxDataViewCtrl *const view = GetView();
        if (GetAttr().HasFont())
            dc->SetFont(GetAttr().GetEffectiveFont(view->GetFont()));
        else
            dc->SetFont(view->GetFont());
    }
#endif

    RenderText(m_value.GetText(), xoffset, rect, dc, state & wxDATAVIEW_CELL_SELECTED ? 0 : state);

    return true;
}

wxSize BitmapTextRenderer::GetSize() const
{
    if (!m_value.GetText().empty())
    {
        wxSize size;
        wxDataViewCtrl *const view = GetView();
        wxClientDC dc(view);
        if (GetAttr().HasFont())
            dc.SetFont(GetAttr().GetEffectiveFont(view->GetFont()));
        else
            dc.SetFont(view->GetFont());

        size = dc.GetTextExtent(m_value.GetText());

        int lines = m_value.GetText().Freq('\n') + 1;
        size.SetHeight(size.GetHeight() * lines);

        if (m_value.GetBitmap().IsOk())
            size.x += m_value.GetBitmap().GetWidth() + 4;
        return size;
    }
    // DPI-scaled fallback size
    int em = Slic3r::GUI::wxGetApp().em_unit();
    return wxSize(8 * em, 2 * em);
}

wxWindow *BitmapTextRenderer::CreateEditorCtrl(wxWindow *parent, wxRect labelRect, const wxVariant &value)
{
    if (can_create_editor_ctrl && !can_create_editor_ctrl())
        return nullptr;

    DataViewBitmapText data;
    data << value;

    m_was_unusable_symbol = false;

    wxPoint position = labelRect.GetPosition();
    if (data.GetBitmap().IsOk())
    {
        const int bmp_width = data.GetBitmap().GetWidth();
        position.x += bmp_width;
        labelRect.SetWidth(labelRect.GetWidth() - bmp_width);
    }

#ifdef __WXMSW__
    // Case when from some reason we try to create next EditorCtrl till old one was not deleted
    if (auto children = parent->GetChildren(); children.GetCount() > 0)
        for (auto child : children)
            if (dynamic_cast<wxTextCtrl *>(child))
            {
                parent->RemoveChild(child);
                child->Destroy();
                break;
            }
#endif // __WXMSW__

    wxTextCtrl *text_editor = new wxTextCtrl(parent, wxID_ANY, data.GetText(), position, labelRect.GetSize(),
                                             wxTE_PROCESS_ENTER);
    text_editor->SetInsertionPointEnd();
    text_editor->SelectAll();

    return text_editor;
}

bool BitmapTextRenderer::GetValueFromEditorCtrl(wxWindow *ctrl, wxVariant &value)
{
    wxTextCtrl *text_editor = wxDynamicCast(ctrl, wxTextCtrl);
    if (!text_editor || text_editor->GetValue().IsEmpty())
        return false;

    m_was_unusable_symbol = Slic3r::GUI::has_illegal_characters(text_editor->GetValue());
    if (m_was_unusable_symbol)
        return false;

    // The icon can't be edited so get its old value and reuse it.
    wxVariant valueOld;
    GetView()->GetModel()->GetValue(valueOld, m_item, /*colName*/ 0);

    DataViewBitmapText bmpText;
    bmpText << valueOld;

    // But replace the text with the value entered by user.
    bmpText.SetText(text_editor->GetValue());

    value << bmpText;
    return true;
}

// ----------------------------------------------------------------------------
// BitmapChoiceRenderer
// ----------------------------------------------------------------------------

bool BitmapChoiceRenderer::SetValue(const wxVariant &value)
{
    m_value << value;
    return true;
}

bool BitmapChoiceRenderer::GetValue(wxVariant &value) const
{
    value << m_value;
    return true;
}

bool BitmapChoiceRenderer::Render(wxRect rect, wxDC *dc, int state)
{
    int xoffset = 0;

    const wxBitmap &icon = m_value.GetBitmap();
    if (icon.IsOk())
    {
        wxSize icon_sz = get_size(icon);

        dc->DrawBitmap(icon, rect.x, rect.y + (rect.height - icon_sz.GetHeight()) / 2);
        xoffset = icon_sz.GetWidth() + 4;

        if (rect.height == 0)
            rect.height = icon_sz.GetHeight();
    }

    // Set text color based on current theme palette (cross-platform)
    {
        bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
        wxColour text_color = is_dark ? wxColour(250, 250, 250) : wxColour(38, 46, 48);

        if (state & wxDATAVIEW_CELL_SELECTED)
            text_color = is_dark ? wxColour(255, 255, 255) : wxColour(38, 46, 48);

        dc->SetTextForeground(text_color);
    }
    RenderText(m_value.GetText(), xoffset, rect, dc, state & wxDATAVIEW_CELL_SELECTED ? 0 : state);

    return true;
}

wxSize BitmapChoiceRenderer::GetSize() const
{
    wxSize sz = GetTextExtent(m_value.GetText());

    if (m_value.GetBitmap().IsOk())
        sz.x += m_value.GetBitmap().GetWidth() + 4;

    return sz;
}

wxWindow *BitmapChoiceRenderer::CreateEditorCtrl(wxWindow *parent, wxRect labelRect, const wxVariant &value)
{
    if (can_create_editor_ctrl && !can_create_editor_ctrl())
        return nullptr;

    std::vector<wxBitmapBundle *> icons = get_extruder_color_icons();
    if (icons.empty())
        return nullptr;

    DataViewBitmapText data;
    data << value;

#ifdef _WIN32
    Slic3r::GUI::BitmapComboBox *c_editor = new Slic3r::GUI::BitmapComboBox(parent, wxID_ANY, wxEmptyString,
#else
    auto c_editor = new wxBitmapComboBox(parent, wxID_ANY, wxEmptyString,
#endif
                                                                            labelRect.GetTopLeft(),
                                                                            wxSize(labelRect.GetWidth(), -1), 0,
                                                                            nullptr, wxCB_READONLY);

#ifdef _WIN32
    Slic3r::GUI::wxGetApp().UpdateDarkUI(c_editor);
#endif

    int def_id = get_default_extruder_idx ? get_default_extruder_idx() : 0;
    c_editor->Append(_L("default"), def_id < 0 ? wxNullBitmap : *icons[def_id]);
    for (size_t i = 0; i < icons.size(); i++)
        c_editor->Append(wxString::Format("%d", i + 1), *icons[i]);

    c_editor->SetSelection(atoi(data.GetText().c_str()));

#ifdef __linux__
    c_editor->Bind(wxEVT_COMBOBOX,
                   [this](wxCommandEvent &evt)
                   {
                       // to avoid event propagation to other sidebar items
                       evt.StopPropagation();
                       // FinishEditing grabs new selection and triggers config update. We better call
                       // it explicitly, automatic update on KILL_FOCUS didn't work on Linux.
                       this->FinishEditing();
                   });
#else
    // to avoid event propagation to other sidebar items
    c_editor->Bind(wxEVT_COMBOBOX, [](wxCommandEvent &evt) { evt.StopPropagation(); });
#endif

    return c_editor;
}

bool BitmapChoiceRenderer::GetValueFromEditorCtrl(wxWindow *ctrl, wxVariant &value)
{
#ifdef _WIN32
    Slic3r::GUI::BitmapComboBox *c = static_cast<Slic3r::GUI::BitmapComboBox *>(ctrl);
#else
    wxBitmapComboBox *c = static_cast<wxBitmapComboBox *>(ctrl);
#endif
    int selection = c->GetSelection();
    if (selection < 0)
        return false;

    DataViewBitmapText bmpText;

    bmpText.SetText(c->GetString(selection));
    bmpText.SetBitmap(c->GetItemBitmap(selection));

    value << bmpText;
    return true;
}

// ----------------------------------------------------------------------------
// TextRenderer
// ----------------------------------------------------------------------------

bool TextRenderer::SetValue(const wxVariant &value)
{
    m_value = value.GetString();
    return true;
}

bool TextRenderer::GetValue(wxVariant &value) const
{
    return false;
}

bool TextRenderer::Render(wxRect rect, wxDC *dc, int state)
{
#ifdef _WIN32
    // workaround for Windows DarkMode : Don't respect to the state & wxDATAVIEW_CELL_SELECTED to avoid update of the text color
    RenderText(m_value, 0, rect, dc, state & wxDATAVIEW_CELL_SELECTED ? 0 : state);
#else
    RenderText(m_value, 0, rect, dc, state);
#endif

    return true;
}

wxSize TextRenderer::GetSize() const
{
    return GetTextExtent(m_value);
}
