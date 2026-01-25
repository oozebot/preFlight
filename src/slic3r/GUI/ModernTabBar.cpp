///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ModernTabBar.hpp"
#include <wx/dcclient.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include "GUI_App.hpp"
#include "libslic3r/PresetBundle.hpp"
#include <wx/menu.h>
#include "wxExtensions.hpp" // for em_unit
#include "Theme.hpp"        // for preFlight theme colors

namespace Slic3r
{
namespace GUI
{

ModernTabBar::ModernTabBar(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 36))
{
    UpdateColors();
    SetBackgroundColour(m_color_bg_normal);

    // Without this, stretch spacers and edges may show white pixels in dark mode
    Bind(wxEVT_ERASE_BACKGROUND,
         [this](wxEraseEvent &event)
         {
             wxDC *dc = event.GetDC();
             if (dc)
             {
                 wxSize size = GetClientSize();
                 dc->SetBrush(wxBrush(GetBackgroundColour()));
                 dc->SetPen(*wxTRANSPARENT_PEN);
                 dc->DrawRectangle(0, 0, size.x, size.y);
             }
         });

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddSpacer(10); // Left margin

    SetSizer(sizer);

    // Search box will be created after all buttons are added
}

void ModernTabBar::AddButton(TabType type, const wxString &label, std::function<void()> callback)
{
    auto *button = CreateStyledButton(label);

    button->Bind(wxEVT_BUTTON, [this, type](wxCommandEvent &) { OnButtonClick(type); });
    button->Bind(wxEVT_ENTER_WINDOW,
                 [button, this](wxMouseEvent &)
                 {
                     if (!button->IsEnabled())
                         return;
                     button->SetBackgroundColour(m_color_bg_hover);
                     button->Refresh();
                 });
    button->Bind(wxEVT_LEAVE_WINDOW,
                 [button, this, type](wxMouseEvent &)
                 {
                     if (!button->IsEnabled())
                         return;
                     button->SetBackgroundColour(IsSelected(type) ? m_color_bg_selected : m_color_bg_normal);
                     button->Refresh();
                 });

    GetSizer()->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    m_tabs.push_back({button, type, callback, true});

    // Select first tab by default
    if (m_tabs.size() == 1)
    {
        SelectTab(type);
    }
}

void ModernTabBar::SelectTab(TabType type)
{
    if (type == m_selected_tab)
        return;

    // Find and execute callback
    for (auto &tab : m_tabs)
    {
        if (tab.type == type && tab.enabled)
        {
            m_selected_tab = type;
            if (tab.callback)
            {
                tab.callback();
            }
            UpdateButtonStyles();
            UpdateSliceButtonVisibility();
            break;
        }
    }
}

void ModernTabBar::EnableTab(TabType type, bool enable)
{
    for (auto &tab : m_tabs)
    {
        if (tab.type == type)
        {
            tab.enabled = enable;
            tab.button->Enable(enable);
            if (!enable)
            {
                // Make background noticeably darker/different from normal tabs
                wxColour disabled_bg = m_color_bg_normal;
                disabled_bg = wxColour(disabled_bg.Red() * 0.7, // Darken by 30%
                                       disabled_bg.Green() * 0.7, disabled_bg.Blue() * 0.7);
                tab.button->SetBackgroundColour(disabled_bg);
                tab.button->SetForegroundColour(m_color_text_disabled);
            }
            else
            {
                UpdateButtonStyles();
            }
            tab.button->Refresh();
            break;
        }
    }
}

void ModernTabBar::OnButtonClick(TabType type)
{
    SelectTab(type);
}

void ModernTabBar::UpdateButtonStyles()
{
    for (const auto &tab : m_tabs)
    {
        // Check if tab is disabled first
        if (!tab.enabled)
        {
            // Keep disabled styling - darker background and disabled text color
            wxColour disabled_bg = m_color_bg_normal;
            disabled_bg = wxColour(disabled_bg.Red() * 0.7, // Darken by 30%
                                   disabled_bg.Green() * 0.7, disabled_bg.Blue() * 0.7);
            tab.button->SetBackgroundColour(disabled_bg);
            tab.button->SetForegroundColour(m_color_text_disabled);
        }
        else if (tab.type == m_selected_tab)
        {
            tab.button->SetBackgroundColour(m_color_bg_normal); // Same as unselected
            tab.button->SetForegroundColour(m_color_text_selected);
            tab.button->SetFont(tab.button->GetFont().GetBaseFont());
        }
        else
        {
            tab.button->SetBackgroundColour(m_color_bg_normal);
            tab.button->SetForegroundColour(m_color_text_normal);
            tab.button->SetFont(tab.button->GetFont().GetBaseFont());
        }
        tab.button->Refresh();
    }
}

wxButton *ModernTabBar::CreateStyledButton(const wxString &label)
{
    auto *button = new wxButton(this, wxID_ANY, label, wxDefaultPosition, wxSize(120, 28), wxBORDER_NONE);

    // Set modern styling
    button->SetBackgroundColour(m_color_bg_normal);
    button->SetForegroundColour(m_color_text_normal);

    // Create rounded corners effect with custom paint
    button->Bind(wxEVT_PAINT,
                 [button, this](wxPaintEvent &evt)
                 {
                     wxPaintDC dc(button);

                     // Get button size
                     wxSize size = button->GetSize();
                     wxRect rect(0, 0, size.x, size.y);

                     bool is_active = false;
                     for (const auto &tab : m_tabs)
                     {
                         if (tab.button == button && tab.type == m_selected_tab)
                         {
                             is_active = true;
                             break;
                         }
                     }

                     const int corner_radius = 8; // Increased from 5 to 8 for more visible rounding

                     // First, fill entire area with parent background color to create corner cutouts
                     dc.SetPen(*wxTRANSPARENT_PEN);
                     dc.SetBrush(wxBrush(GetBackgroundColour()));
                     dc.DrawRectangle(0, 0, size.x, size.y);

                     // Then draw the rounded button background
                     dc.SetBrush(wxBrush(button->GetBackgroundColour()));
                     dc.SetPen(*wxTRANSPARENT_PEN);
                     dc.DrawRoundedRectangle(0, 0, size.x, size.y, corner_radius);

                     // Draw rounded border for active tab
                     if (is_active)
                     {
                         wxColour brand_color(234, 160, 50); // #EAA032
                         dc.SetPen(wxPen(brand_color, 1));
                         dc.SetBrush(*wxTRANSPARENT_BRUSH);
                         dc.DrawRoundedRectangle(0, 0, size.x - 1, size.y - 1, corner_radius);
                     }

                     // Draw text
                     dc.SetTextForeground(button->GetForegroundColour());
                     dc.SetFont(button->GetFont());

                     wxString label = button->GetLabel();
                     wxCoord textWidth, textHeight;
                     dc.GetTextExtent(label, &textWidth, &textHeight);

                     int x = (size.x - textWidth) / 2;
                     int y = (size.y - textHeight) / 2;
                     dc.DrawText(label, x, y);
                 });

    return button;
}

void ModernTabBar::UpdateColors()
{
    bool is_dark = wxGetApp().dark_mode();

    if (is_dark)
    {
        // Dark mode colors
        m_color_bg_normal = wxColour(60, 60, 60);
        m_color_bg_hover = wxColour(80, 80, 80);
        m_color_bg_selected = wxColour(90, 90, 90);
        m_color_text_normal = wxColour(200, 200, 200);
        m_color_text_selected = wxColour(255, 255, 255);
        m_color_text_disabled = wxColour(80, 80, 80); // Very dark gray, much more visible as disabled
        m_color_border = wxColour(80, 80, 80);
    }
    else
    {
        // Light mode colors
        m_color_bg_normal = wxColour(240, 240, 240);
        m_color_bg_hover = wxColour(232, 232, 232);
        m_color_bg_selected = wxColour(255, 255, 255);
        m_color_text_normal = wxColour(102, 102, 102);
        m_color_text_selected = wxColour(51, 51, 51);
        m_color_text_disabled = wxColour(180, 180, 180); // Light gray, clearly different from normal
        m_color_border = wxColour(224, 224, 224);
    }

    // Update panel background
    SetBackgroundColour(m_color_bg_normal);
}

void ModernTabBar::sys_color_changed()
{
    // Update colors based on new theme
    UpdateColors();

    // Update all button styles
    UpdateButtonStyles();

    // Update slice button colors if it exists
    if (m_slice_button)
    {
        m_slice_button->Refresh();
    }

    // Refresh the panel
    Refresh();
}

void ModernTabBar::AddSliceButton(std::function<void()> slice_callback, std::function<void()> export_callback)
{
    m_slice_callback = slice_callback;
    m_export_callback = export_callback;

    // Create a custom button that matches Orca Slicer style
    m_slice_button = new wxButton(this, wxID_ANY, "", wxDefaultPosition, wxSize(130, 28), wxBORDER_NONE);

    // Bind paint event for custom drawing
    m_slice_button->Bind(
        wxEVT_PAINT,
        [this](wxPaintEvent &event)
        {
            wxPaintDC dc(m_slice_button);
            wxSize size = m_slice_button->GetSize();

            // Colors
            wxColour dark_bg = Theme::Complementary::WX_COLOR;   // Complementary tan/beige background #E2BA87
            wxColour orange = Theme::Primary::WX_COLOR;          // Brand color #EAA032
            wxColour orange_hover(244, 180, 80);                 // Lighter hover state
            wxColour text_color = *wxBLACK;                      // Black text for contrast on tan background
            wxColour disabled_bg(180, 180, 180);                 // Gray for disabled state
            wxColour disabled_text(120, 120, 120);               // Darker gray for disabled text
            const int corner_radius = 6;                         // More rounded corners
            const int dropdown_width = m_show_dropdown ? 28 : 0; // Only show dropdown area if needed

            // First, clear the entire button area with the parent background color
            dc.SetBrush(wxBrush(GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, size.x, size.y);

            // Choose colors based on enabled state
            wxColour bg_color = m_slice_button_enabled ? dark_bg : disabled_bg;
            wxColour border_color = m_slice_button_enabled ? orange : disabled_bg;
            wxColour current_text_color = m_slice_button_enabled ? text_color : disabled_text;

            // Draw background with rounded corners (match border dimensions)
            dc.SetBrush(wxBrush(bg_color));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRoundedRectangle(0, 0, size.x - 1, size.y - 1, corner_radius);

            // Draw orange accent on the left (dropdown area) only if dropdown is shown
            if (m_show_dropdown)
            {
                wxColour accent_color = m_slice_button_pressed ? orange_hover : orange;
                dc.SetBrush(wxBrush(accent_color));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRoundedRectangle(1, 1, dropdown_width - 1, size.y - 2, corner_radius - 1);

                // Draw dropdown arrow (chevron)
                dc.SetPen(wxPen(*wxWHITE, 2));
                int arrow_x = dropdown_width / 2;
                int arrow_y = size.y / 2 - 2;
                dc.DrawLine(arrow_x - 4, arrow_y, arrow_x, arrow_y + 4);
                dc.DrawLine(arrow_x, arrow_y + 4, arrow_x + 4, arrow_y);
            }

            // Draw thin border around entire button
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(border_color, 1));
            dc.DrawRoundedRectangle(0, 0, size.x - 1, size.y - 1, corner_radius);

            // Draw text
            dc.SetTextForeground(current_text_color);
            dc.SetFont(GetFont());
            wxString label = m_has_sliced_object ? _L("Export") : _L("Slice plate");
            wxSize text_size = dc.GetTextExtent(label);
            // Center text - if dropdown shown, offset by dropdown width; otherwise center in full button
            int text_x = m_show_dropdown ? (dropdown_width + (size.x - dropdown_width - text_size.x) / 2)
                                         : ((size.x - text_size.x) / 2);
            int text_y = (size.y - text_size.y) / 2;
            dc.DrawText(label, text_x, text_y);
        });

    // Handle mouse events
    m_slice_button->Bind(wxEVT_LEFT_DOWN,
                         [this](wxMouseEvent &event)
                         {
                             if (!m_slice_button_enabled)
                                 return;

                             wxPoint pos = event.GetPosition();
                             m_slice_button_pressed = true;
                             m_slice_button->Refresh();

                             const int dropdown_width = m_show_dropdown ? 28 : 0;
                             if (m_show_dropdown && pos.x <= dropdown_width)
                             {
                                 // Clicked on dropdown area - show menu
                                 wxMenu menu;
                                 const int ID_SAVE_LOCALLY = wxID_HIGHEST + 1;
                                 const int ID_SEND_TO_PRINTER = wxID_HIGHEST + 2;
                                 menu.Append(ID_SAVE_LOCALLY, _L("Save locally"));
                                 menu.Append(ID_SEND_TO_PRINTER, _L("Send to Printer"));

                                 menu.Bind(wxEVT_MENU,
                                           [this, ID_SAVE_LOCALLY, ID_SEND_TO_PRINTER](wxCommandEvent &evt)
                                           {
                                               if (evt.GetId() == ID_SAVE_LOCALLY)
                                               {
                                                   if (m_export_callback)
                                                       m_export_callback();
                                               }
                                               else if (evt.GetId() == ID_SEND_TO_PRINTER)
                                               {
                                                   if (m_send_to_printer_callback)
                                                       m_send_to_printer_callback();
                                               }
                                           });

                                 // Show menu at button position
                                 wxPoint menu_pos = m_slice_button->GetPosition();
                                 menu_pos.y += m_slice_button->GetSize().y;
                                 PopupMenu(&menu, menu_pos);
                             }
                             else
                             {
                                 // Clicked on main button area
                                 if (m_has_sliced_object && m_export_callback)
                                 {
                                     m_export_callback();
                                 }
                                 else if (m_slice_callback)
                                 {
                                     m_slice_callback();
                                 }
                             }
                         });

    m_slice_button->Bind(wxEVT_LEFT_UP,
                         [this](wxMouseEvent &event)
                         {
                             m_slice_button_pressed = false;
                             m_slice_button->Refresh();
                         });

    m_slice_button->Bind(wxEVT_ENTER_WINDOW,
                         [this](wxMouseEvent &event)
                         {
                             if (!m_slice_button_enabled)
                             {
                                 m_slice_button->SetToolTip(_L("Add objects to the platter to enable slicing"));
                             }
                             else
                             {
                                 m_slice_button->SetToolTip(m_has_sliced_object ? _L("Export G-code")
                                                                                : _L("Slice the plate"));
                             }
                         });

    m_slice_button->Bind(wxEVT_LEAVE_WINDOW,
                         [this](wxMouseEvent &event)
                         {
                             m_slice_button_pressed = false;
                             m_slice_button->Refresh();
                         });

    // Add stretch spacer to push button to the right
    GetSizer()->AddStretchSpacer(1);

    // Add the button aligned to the right
    GetSizer()->Add(m_slice_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
}

void ModernTabBar::UpdateSliceButtonState(bool has_sliced_object)
{
    if (!m_slice_button)
        return;

    m_has_sliced_object = has_sliced_object;

    // Update dropdown visibility: only show in Export mode when printer is connected
    m_show_dropdown = has_sliced_object && IsPrinterConnected();

    // Update button visibility based on current tab
    UpdateSliceButtonVisibility();

    m_slice_button->SetLabel(has_sliced_object ? _L("Export") : _L("Slice"));
    m_slice_button->Refresh();
}

void ModernTabBar::HideSliceButton()
{
    if (m_slice_button)
    {
        m_slice_button->Hide();
    }
}

void ModernTabBar::ShowSliceButton()
{
    if (m_slice_button)
    {
        m_slice_button->Show();
    }
}

void ModernTabBar::UpdateSliceButtonVisibility()
{
    if (!m_slice_button)
        return;

    // In Slice mode (not sliced yet): only show on Prepare tab
    // In Export mode (already sliced): show on any tab
    if (m_has_sliced_object)
    {
        // Export mode - always visible
        m_slice_button->Show();
    }
    else
    {
        // Slice mode - only visible on Prepare tab
        bool on_prepare_tab = (m_selected_tab == TAB_PREPARE);
        m_slice_button->Show(on_prepare_tab);
    }

    Layout();
}

void ModernTabBar::EnableSliceButton(bool enable)
{
    if (!m_slice_button)
        return;

    m_slice_button_enabled = enable;
    m_slice_button->Refresh();
}

bool ModernTabBar::IsPrinterConnected() const
{
    // Check if a physical printer with print_host is configured
    auto *selected_printer_config = wxGetApp().preset_bundle->physical_printers.get_selected_printer_config();
    if (!selected_printer_config)
        return false;

    const auto print_host_opt = selected_printer_config->option<ConfigOptionString>("print_host");
    return print_host_opt != nullptr && !print_host_opt->value.empty();
}

void ModernTabBar::SetSendToPrinterCallback(std::function<void()> callback)
{
    m_send_to_printer_callback = callback;
}

void ModernTabBar::RefreshPrinterConnectionState()
{
    if (!m_slice_button)
        return;

    // Re-evaluate dropdown visibility based on current printer connection
    bool was_showing_dropdown = m_show_dropdown;
    m_show_dropdown = m_has_sliced_object && IsPrinterConnected();

    // Only refresh if state changed
    if (was_showing_dropdown != m_show_dropdown)
    {
        m_slice_button->Refresh();
    }
}

} // namespace GUI
} // namespace Slic3r
