///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ModernTabBar.hpp"
#include "Widgets/UIColors.hpp"
#include <wx/dcclient.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <wx/menu.h>
#include "wxExtensions.hpp" // for em_unit
#include "Widgets/CustomMenu.hpp"
#include "Theme.hpp" // for preFlight theme colors
#include <wx/dcbuffer.h>

namespace Slic3r
{
namespace GUI
{

// DPI scaling helper functions - use em_unit system for consistency with Sidebar
// At 100% DPI, em_unit() returns 10, so these return the base pixel values
static int GetScaledTabHeight()
{
    return static_cast<int>(3.6 * wxGetApp().em_unit()); // 36px at 100%
}

static int GetScaledButtonWidth()
{
    return static_cast<int>(12 * wxGetApp().em_unit()); // 120px at 100%
}

static int GetScaledButtonHeight()
{
    return static_cast<int>(2.8 * wxGetApp().em_unit()); // 28px at 100%
}

static wxSize GetScaledButtonSize()
{
    return wxSize(GetScaledButtonWidth(), GetScaledButtonHeight());
}

static int GetScaledSliceButtonWidth()
{
    return static_cast<int>(16 * wxGetApp().em_unit()); // 160px at 100%
}

static wxSize GetScaledSliceButtonSize()
{
    return wxSize(GetScaledSliceButtonWidth(), GetScaledButtonHeight());
}

static int GetScaledCornerRadius()
{
    return static_cast<int>(0.8 * wxGetApp().em_unit()); // 8px at 100%
}

static int GetScaledSmallCornerRadius()
{
    return static_cast<int>(0.6 * wxGetApp().em_unit()); // 6px at 100%
}

static int GetScaledDropdownWidth()
{
    return GetScaledButtonHeight(); // Same as button height (28px at 100%)
}

static int GetScaledDotSize()
{
    return static_cast<int>(0.8 * wxGetApp().em_unit()); // 8px at 100%
}

static int GetScaledMargin()
{
    return wxGetApp().em_unit(); // 10px at 100%
}

static int GetScaledSmallMargin()
{
    return wxGetApp().em_unit() / 2; // 5px at 100%
}

static int GetScaledDotTextGap()
{
    return static_cast<int>(0.6 * wxGetApp().em_unit()); // 6px at 100%
}

static int GetScaledHMargin()
{
    return static_cast<int>(0.8 * wxGetApp().em_unit()); // 8px at 100%
}

static int GetScaledChevronPenWidth()
{
    return std::max(1, static_cast<int>(0.2 * wxGetApp().em_unit())); // 2px at 100%, min 1px
}

static int GetScaledChevronOffset()
{
    return static_cast<int>(0.2 * wxGetApp().em_unit()); // 2px at 100%
}

static int GetScaledChevronArrowSize()
{
    return static_cast<int>(0.4 * wxGetApp().em_unit()); // 4px at 100%
}

ModernTabBar::ModernTabBar(wxWindow *parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, GetScaledTabHeight()))
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

    // Fix for custom-painted buttons not repainting after window is obscured and uncovered
    // Bind to top-level window's activation event to refresh all buttons
    wxWindow *topLevel = wxGetTopLevelParent(this);
    if (topLevel)
    {
        topLevel->Bind(wxEVT_ACTIVATE,
                       [this](wxActivateEvent &event)
                       {
                           event.Skip(); // Let other handlers process this too
                           if (event.GetActive())
                           {
                               // Window became active - refresh all custom-painted buttons
                               for (const auto &tab : m_tabs)
                               {
                                   tab.button->Refresh();
                               }
                               if (m_slice_button)
                                   m_slice_button->Refresh();
                               if (m_printer_webview_btn)
                                   m_printer_webview_btn->Refresh();
                               if (m_print_settings_btn)
                                   m_print_settings_btn->Refresh();
                               if (m_filament_settings_btn)
                                   m_filament_settings_btn->Refresh();
                               if (m_printer_settings_btn)
                                   m_printer_settings_btn->Refresh();
                               if (m_search_btn)
                                   m_search_btn->Refresh();
                               if (m_settings_dropdown_btn)
                                   m_settings_dropdown_btn->Refresh();
                           }
                       });
    }

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddSpacer(GetScaledMargin()); // Left margin (scaled)

    SetSizer(sizer);

    // Responsive layout: toggle between expanded (3 buttons) and collapsed (dropdown) settings
    Bind(wxEVT_SIZE,
         [this](wxSizeEvent &event)
         {
             event.Skip();
             UpdateSettingsLayout();
         });
}

void ModernTabBar::AddButton(TabType type, const wxString &label, std::function<void()> callback)
{
    auto *button = CreateStyledButton(label);

    button->Bind(wxEVT_LEFT_UP, [this, type](wxMouseEvent &) { OnButtonClick(type); });
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

    GetSizer()->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, GetScaledSmallMargin());

    m_tabs.push_back({button, type, callback, true});

    // Select first tab by default
    if (m_tabs.size() == 1)
    {
        SelectTab(type);
    }
}

void ModernTabBar::AddSettingsDropdownButton(std::function<void(TabType)> callback)
{
    m_settings_callback = callback;

    // Helper lambda: common click logic for selecting a settings tab
    auto select_settings_tab = [this](TabType selected_type)
    {
        if (!m_settings_callback)
            return;

        // If leaving the printer webview tab, hide its content first
        if (m_selected_tab == TAB_PRINTER_WEBVIEW && m_printer_webview_btn)
        {
            if (auto *mainframe = dynamic_cast<MainFrame *>(wxGetApp().GetTopWindow()))
            {
                mainframe->hide_printer_webview_content();
            }
            m_printer_webview_btn->Refresh();
        }

        m_selected_tab = selected_type;
        UpdateButtonStyles();
        UpdateSliceButtonVisibility();
        m_settings_callback(selected_type);
    };

    // --- Create 3 individual settings buttons (shown when expanded) ---

    auto create_individual_settings_btn = [this, select_settings_tab](const wxString &label, TabType type) -> wxPanel *
    {
        wxPanel *btn = CreateStyledButton(label);

        btn->Bind(wxEVT_LEFT_UP, [select_settings_tab, type](wxMouseEvent &) { select_settings_tab(type); });
        btn->Bind(wxEVT_ENTER_WINDOW,
                  [btn, this](wxMouseEvent &)
                  {
                      btn->SetBackgroundColour(m_color_bg_hover);
                      btn->Refresh();
                  });
        btn->Bind(wxEVT_LEAVE_WINDOW,
                  [btn, this, type](wxMouseEvent &)
                  {
                      btn->SetBackgroundColour((m_selected_tab == type) ? m_color_bg_selected : m_color_bg_normal);
                      btn->Refresh();
                  });

        // Initially hidden - UpdateSettingsLayout() will show them if there's enough space
        btn->Hide();
        GetSizer()->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, GetScaledSmallMargin());
        return btn;
    };

    m_print_settings_btn = create_individual_settings_btn(_L("Print Settings"), TAB_PRINT_SETTINGS);
    m_filament_settings_btn = create_individual_settings_btn(_L("Filament Settings"), TAB_FILAMENTS);
    m_printer_settings_btn = create_individual_settings_btn(_L("Printer Settings"), TAB_PRINTERS);

    // --- preFlight: Search action button (always visible with expanded settings) ---
    {
        m_search_btn = CreateStyledButton(_L("Search Settings"));

        m_search_btn->Bind(wxEVT_LEFT_UP, [](wxMouseEvent &) { wxGetApp().show_search_dialog(); });
        m_search_btn->Bind(wxEVT_ENTER_WINDOW,
                           [this](wxMouseEvent &)
                           {
                               m_search_btn->SetBackgroundColour(m_color_bg_hover);
                               m_search_btn->Refresh();
                           });
        m_search_btn->Bind(wxEVT_LEAVE_WINDOW,
                           [this](wxMouseEvent &)
                           {
                               m_search_btn->SetBackgroundColour(m_color_bg_normal);
                               m_search_btn->Refresh();
                           });

        m_search_btn->Hide();
        GetSizer()->Add(m_search_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, GetScaledSmallMargin());
    }

    // --- Create collapsed Settings dropdown button (shown when not enough space) ---

    m_settings_dropdown_btn = new wxPanel(this, wxID_ANY, wxDefaultPosition, GetScaledButtonSize(), wxBORDER_NONE);
    m_settings_dropdown_btn->SetLabel(_L("Settings"));
    m_settings_dropdown_btn->SetBackgroundColour(m_color_bg_normal);
    m_settings_dropdown_btn->SetForegroundColour(m_color_text_normal);
    m_settings_dropdown_btn->SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Custom paint handler for Settings button with border when active
    m_settings_dropdown_btn->Bind(wxEVT_PAINT,
                                  [this](wxPaintEvent &)
                                  {
                                      wxAutoBufferedPaintDC dc(m_settings_dropdown_btn);
                                      wxSize size = m_settings_dropdown_btn->GetSize();

                                      bool is_active = (m_selected_tab == TAB_PRINT_SETTINGS ||
                                                        m_selected_tab == TAB_FILAMENTS ||
                                                        m_selected_tab == TAB_PRINTERS);

                                      const int corner_radius = GetScaledCornerRadius();

                                      // Fill entire area with parent background color
                                      dc.SetPen(*wxTRANSPARENT_PEN);
                                      dc.SetBrush(wxBrush(GetBackgroundColour()));
                                      dc.DrawRectangle(0, 0, size.x, size.y);

                                      // Draw the rounded button background
                                      dc.SetBrush(wxBrush(m_settings_dropdown_btn->GetBackgroundColour()));
                                      dc.SetPen(*wxTRANSPARENT_PEN);
                                      dc.DrawRoundedRectangle(0, 0, size.x, size.y, corner_radius);

                                      // Draw rounded border for active state
                                      if (is_active)
                                      {
                                          wxColour brand_color(234, 160, 50); // #EAA032
                                          dc.SetPen(wxPen(brand_color, 1));
                                          dc.SetBrush(*wxTRANSPARENT_BRUSH);
                                          dc.DrawRoundedRectangle(0, 0, size.x - 1, size.y - 1, corner_radius);
                                      }

                                      // Draw text
                                      dc.SetTextForeground(m_settings_dropdown_btn->GetForegroundColour());
                                      dc.SetFont(m_settings_dropdown_btn->GetFont());

                                      wxString label = m_settings_dropdown_btn->GetLabel();
                                      wxCoord textWidth, textHeight;
                                      dc.GetTextExtent(label, &textWidth, &textHeight);

                                      int x = (size.x - textWidth) / 2;
                                      int y = (size.y - textHeight) / 2;
                                      dc.DrawText(label, x, y);
                                  });

    m_settings_dropdown_btn->Bind(
        wxEVT_LEFT_UP,
        [this, select_settings_tab](wxMouseEvent &)
        {
            wxMenu menu;
            wxMenuItem *print_item = menu.Append(static_cast<int>(TAB_PRINT_SETTINGS), _L("Print Settings"));
            print_item->SetBitmap(*get_bmp_bundle("cog"));
            wxMenuItem *filament_item = menu.Append(static_cast<int>(TAB_FILAMENTS), _L("Filament Settings"));
            filament_item->SetBitmap(*get_bmp_bundle("spool"));
            wxMenuItem *printer_item = menu.Append(static_cast<int>(TAB_PRINTERS), _L("Printer Settings"));
            printer_item->SetBitmap(*get_bmp_bundle("printer"));
            menu.AppendSeparator();
            // preFlight: use wxID_HIGHEST+1 to avoid collision with TabType enum values
            const int search_menu_id = wxID_HIGHEST + 1;
            wxMenuItem *search_item = menu.Append(search_menu_id, _L("Search Settings"));
            search_item->SetBitmap(*get_bmp_bundle("search"));

            // Position menu at bottom-left of button (like standard menu bar menus)
            wxPoint menu_pos(0, m_settings_dropdown_btn->GetSize().y);

            // Use CustomMenu for consistent theming
            auto customMenu = CustomMenu::FromWxMenu(&menu, m_settings_dropdown_btn);
            if (customMenu)
            {
                customMenu->SetCallback(static_cast<int>(TAB_PRINT_SETTINGS),
                                        [select_settings_tab]() { select_settings_tab(TAB_PRINT_SETTINGS); });
                customMenu->SetCallback(static_cast<int>(TAB_FILAMENTS),
                                        [select_settings_tab]() { select_settings_tab(TAB_FILAMENTS); });
                customMenu->SetCallback(static_cast<int>(TAB_PRINTERS),
                                        [select_settings_tab]() { select_settings_tab(TAB_PRINTERS); });
                customMenu->SetCallback(search_menu_id, []() { wxGetApp().show_search_dialog(); });

                customMenu->KeepAliveUntilDismissed(customMenu);
                if (!customMenu->GetParent())
                    customMenu->Create(m_settings_dropdown_btn);
                wxPoint screenPos = m_settings_dropdown_btn->ClientToScreen(menu_pos);
                customMenu->ShowAt(screenPos, m_settings_dropdown_btn);
            }
            else
            {
                // Fallback to native menu
                int selection = m_settings_dropdown_btn->GetPopupMenuSelectionFromUser(menu, menu_pos);
                if (selection == search_menu_id)
                    wxGetApp().show_search_dialog();
                else if (selection != wxID_NONE)
                    select_settings_tab(static_cast<TabType>(selection));
            }
        });

    m_settings_dropdown_btn->Bind(wxEVT_ENTER_WINDOW,
                                  [this](wxMouseEvent &)
                                  {
                                      m_settings_dropdown_btn->SetBackgroundColour(m_color_bg_hover);
                                      m_settings_dropdown_btn->Refresh();
                                  });
    m_settings_dropdown_btn->Bind(wxEVT_LEAVE_WINDOW,
                                  [this](wxMouseEvent &)
                                  {
                                      bool is_settings_selected = (m_selected_tab == TAB_PRINT_SETTINGS ||
                                                                   m_selected_tab == TAB_FILAMENTS ||
                                                                   m_selected_tab == TAB_PRINTERS);
                                      m_settings_dropdown_btn->SetBackgroundColour(
                                          is_settings_selected ? m_color_bg_selected : m_color_bg_normal);
                                      m_settings_dropdown_btn->Refresh();
                                  });

    GetSizer()->Add(m_settings_dropdown_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, GetScaledSmallMargin());
}

void ModernTabBar::UpdateSettingsLayout(bool force)
{
    // Nothing to do if individual buttons weren't created yet
    if (!m_print_settings_btn || !m_settings_dropdown_btn)
        return;

    // Use INTENDED sizes (MinSize/scaled constants) rather than current GetSize(),
    // because the sizer may have already compressed buttons when space is tight.
    const int margin = GetScaledSmallMargin();
    const int left_margin = GetScaledMargin();
    const int btn_w = GetScaledButtonWidth();

    int fixed_width = left_margin; // Left spacer

    // Tab buttons (Prepare, Preview) - use intended width
    fixed_width += static_cast<int>(m_tabs.size()) * (btn_w + margin);

    // Printer webview button (if present) - use MinSize since it's dynamically sized
    if (m_printer_webview_btn && m_printer_webview_btn->IsShown())
        fixed_width += m_printer_webview_btn->GetMinSize().GetWidth() + margin;

    // Slice button + its right margin - use intended width
    if (m_slice_button && m_slice_button->IsShown())
        fixed_width += GetScaledSliceButtonWidth() + GetScaledMargin();

    // Calculate width for expanded mode (3 individual settings buttons + search button)
    int expanded_settings_width = 4 * (btn_w + margin);

    int total_expanded = fixed_width + expanded_settings_width;

    // Current available width
    int available_width = GetSize().GetWidth();

    bool should_expand = (available_width >= total_expanded);

    if (!force && should_expand == m_settings_expanded)
        return; // No change needed

    m_settings_expanded = should_expand;

    if (m_settings_expanded)
    {
        // Show individual buttons, hide dropdown
        m_print_settings_btn->Show();
        m_filament_settings_btn->Show();
        m_printer_settings_btn->Show();
        if (m_search_btn)
            m_search_btn->Show();
        m_settings_dropdown_btn->Hide();
    }
    else
    {
        // Hide individual buttons, show dropdown
        m_print_settings_btn->Hide();
        m_filament_settings_btn->Hide();
        m_printer_settings_btn->Hide();
        if (m_search_btn)
            m_search_btn->Hide();
        m_settings_dropdown_btn->Show();
    }

    Layout();
    Refresh();
}

void ModernTabBar::SelectTab(TabType type)
{
    if (type == m_selected_tab)
        return;

    // If leaving the printer webview tab, hide its content first
    if (m_selected_tab == TAB_PRINTER_WEBVIEW && m_printer_webview_btn)
    {
        // Notify MainFrame to hide the webview content
        if (auto *mainframe = dynamic_cast<MainFrame *>(wxGetApp().GetTopWindow()))
        {
            mainframe->hide_printer_webview_content();
        }
        m_printer_webview_btn->Refresh();
    }

    // preFlight: settings tabs (Print/Filament/Printer) are not in m_tabs —
    // they are handled via m_settings_callback from AddSettingsDropdownButton.
    if (m_settings_callback && (type == TAB_PRINT_SETTINGS || type == TAB_FILAMENTS || type == TAB_PRINTERS))
    {
        m_selected_tab = type;
        UpdateButtonStyles();
        UpdateSliceButtonVisibility();
        m_settings_callback(type);
        return;
    }

    // Find and execute callback for regular tabs (Prepare, Preview, etc.)
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
                // Use centralized disabled colors from UIColors
                tab.button->SetBackgroundColour(UIColors::TabBackgroundDisabled());
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
            // Use centralized disabled colors from UIColors
            tab.button->SetBackgroundColour(UIColors::TabBackgroundDisabled());
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

    // Also update printer webview button if it exists
    if (m_printer_webview_btn)
    {
        m_printer_webview_btn->SetBackgroundColour((m_selected_tab == TAB_PRINTER_WEBVIEW) ? m_color_bg_selected
                                                                                           : m_color_bg_normal);
        m_printer_webview_btn->Refresh();
    }

    // Update Settings dropdown button if it exists
    if (m_settings_dropdown_btn)
    {
        bool is_settings_selected = (m_selected_tab == TAB_PRINT_SETTINGS || m_selected_tab == TAB_FILAMENTS ||
                                     m_selected_tab == TAB_PRINTERS);
        m_settings_dropdown_btn->SetBackgroundColour(is_settings_selected ? m_color_bg_selected : m_color_bg_normal);
        m_settings_dropdown_btn->SetForegroundColour(is_settings_selected ? m_color_text_selected
                                                                          : m_color_text_normal);
        m_settings_dropdown_btn->Refresh();
    }

    // Update individual settings buttons if they exist
    auto update_settings_btn = [this](wxPanel *btn, TabType type)
    {
        if (!btn)
            return;
        bool is_selected = (m_selected_tab == type);
        btn->SetBackgroundColour(is_selected ? m_color_bg_normal : m_color_bg_normal);
        btn->SetForegroundColour(is_selected ? m_color_text_selected : m_color_text_normal);
        btn->Refresh();
    };
    update_settings_btn(m_print_settings_btn, TAB_PRINT_SETTINGS);
    update_settings_btn(m_filament_settings_btn, TAB_FILAMENTS);
    update_settings_btn(m_printer_settings_btn, TAB_PRINTERS);

    // Search button is an action button, never selected
    if (m_search_btn)
    {
        m_search_btn->SetBackgroundColour(m_color_bg_normal);
        m_search_btn->SetForegroundColour(m_color_text_normal);
        m_search_btn->Refresh();
    }
}

wxPanel *ModernTabBar::CreateStyledButton(const wxString &label)
{
    // preFlight: Use wxPanel instead of wxButton — wxButton on GTK3 has native theme rendering
    // that can't be fully suppressed, producing inconsistent custom-drawn buttons.
    // wxPanel with wxBG_STYLE_PAINT gives us a clean canvas on all platforms.
    auto *button = new wxPanel(this, wxID_ANY, wxDefaultPosition, GetScaledButtonSize(), wxBORDER_NONE);
    button->SetLabel(label);

    // Set modern styling
    button->SetBackgroundColour(m_color_bg_normal);
    button->SetForegroundColour(m_color_text_normal);
    button->SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Create rounded corners effect with custom paint
    button->Bind(wxEVT_PAINT,
                 [button, this](wxPaintEvent &evt)
                 {
                     wxAutoBufferedPaintDC dc(button);

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
                     // Also check individual settings buttons
                     if (!is_active)
                     {
                         if ((button == m_print_settings_btn && m_selected_tab == TAB_PRINT_SETTINGS) ||
                             (button == m_filament_settings_btn && m_selected_tab == TAB_FILAMENTS) ||
                             (button == m_printer_settings_btn && m_selected_tab == TAB_PRINTERS))
                         {
                             is_active = true;
                         }
                     }

                     const int corner_radius = GetScaledCornerRadius();

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
        // Dark mode colors - preFlight warm tint (centralized in UIColors)
        m_color_bg_normal = UIColors::TabBackgroundNormalDark();
        m_color_bg_hover = UIColors::TabBackgroundHoverDark();
        m_color_bg_selected = UIColors::TabBackgroundSelectedDark();
        m_color_text_normal = UIColors::TabTextNormalDark();
        m_color_text_selected = UIColors::TabTextSelectedDark();
        m_color_text_disabled = UIColors::TabTextDisabledDark();
        m_color_border = UIColors::TabBorderDark();
    }
    else
    {
        // Light mode colors - preFlight warm tint (centralized in UIColors)
        m_color_bg_normal = UIColors::TabBackgroundNormalLight();
        m_color_bg_hover = UIColors::TabBackgroundHoverLight();
        m_color_bg_selected = UIColors::TabBackgroundSelectedLight();
        m_color_text_normal = UIColors::TabTextNormalLight();
        m_color_text_selected = UIColors::TabTextSelectedLight();
        m_color_text_disabled = UIColors::TabTextDisabledLight();
        m_color_border = UIColors::TabBorderLight();
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

    // Update printer webview button colors if it exists
    if (m_printer_webview_btn)
    {
        m_printer_webview_btn->SetBackgroundColour((m_selected_tab == TAB_PRINTER_WEBVIEW) ? m_color_bg_selected
                                                                                           : m_color_bg_normal);
        m_printer_webview_btn->Refresh();
    }

    // Update Settings dropdown button colors if it exists
    if (m_settings_dropdown_btn)
    {
        m_settings_dropdown_btn->Refresh();
    }

    // Update individual settings button colors
    for (wxPanel *btn : {m_print_settings_btn, m_filament_settings_btn, m_printer_settings_btn, m_search_btn})
    {
        if (btn)
            btn->Refresh();
    }

    // Refresh the panel
    Refresh();
}

void ModernTabBar::msw_rescale()
{
    // Update panel height
    SetMinSize(wxSize(-1, GetScaledTabHeight()));

    // Update all tab buttons
    wxSize button_size = GetScaledButtonSize();
    for (auto &tab : m_tabs)
    {
        tab.button->SetMinSize(button_size);
        tab.button->SetSize(button_size);
    }

    // Update settings dropdown button
    if (m_settings_dropdown_btn)
    {
        m_settings_dropdown_btn->SetMinSize(button_size);
        m_settings_dropdown_btn->SetSize(button_size);
    }

    // Update individual settings buttons
    for (wxPanel *btn : {m_print_settings_btn, m_filament_settings_btn, m_printer_settings_btn, m_search_btn})
    {
        if (btn)
        {
            btn->SetMinSize(button_size);
            btn->SetSize(button_size);
        }
    }

    // Update slice button
    if (m_slice_button)
    {
        wxSize slice_size = GetScaledSliceButtonSize();
        m_slice_button->SetMinSize(slice_size);
        m_slice_button->SetSize(slice_size);
    }

    // Update printer webview button - size to fit printer name, minimum standard width
    if (m_printer_webview_btn)
    {
        wxClientDC dc(m_printer_webview_btn);
        dc.SetFont(m_printer_webview_btn->GetFont());
        wxCoord tw = 0, th = 0;
        dc.GetTextExtent(m_printer_webview_name, &tw, &th);
        int needed_w = 2 * GetScaledHMargin() + GetScaledDotSize() + GetScaledDotTextGap() + tw;
        int btn_w = std::max(needed_w, GetScaledButtonWidth());
        wxSize btn_sz(btn_w, GetScaledButtonHeight());
        m_printer_webview_btn->SetMinSize(btn_sz);
        m_printer_webview_btn->SetSize(btn_sz);
    }

    // Refresh all buttons to trigger repaint with new scaled values
    for (auto &tab : m_tabs)
    {
        tab.button->Refresh();
    }
    if (m_settings_dropdown_btn)
        m_settings_dropdown_btn->Refresh();
    for (wxPanel *btn : {m_print_settings_btn, m_filament_settings_btn, m_printer_settings_btn, m_search_btn})
    {
        if (btn)
            btn->Refresh();
    }
    if (m_slice_button)
        m_slice_button->Refresh();
    if (m_printer_webview_btn)
        m_printer_webview_btn->Refresh();

    // Re-evaluate expanded/collapsed after rescale since button sizes changed
    UpdateSettingsLayout(true);
}

void ModernTabBar::AddSliceButton(std::function<void()> slice_callback, std::function<void()> export_callback)
{
    m_slice_callback = slice_callback;
    m_export_callback = export_callback;

    // Create a custom button that matches Orca Slicer style
    m_slice_button = new wxPanel(this, wxID_ANY, wxDefaultPosition, GetScaledSliceButtonSize(), wxBORDER_NONE);
    m_slice_button->SetMinSize(GetScaledSliceButtonSize());
    m_slice_button->SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Bind paint event for custom drawing
    m_slice_button->Bind(
        wxEVT_PAINT,
        [this](wxPaintEvent &event)
        {
            wxAutoBufferedPaintDC dc(m_slice_button);
            wxSize size = m_slice_button->GetSize();

            // Colors
            wxColour dark_bg = Theme::Complementary::WX_COLOR; // Complementary tan/beige background #E2BA87
            wxColour orange = Theme::Primary::WX_COLOR;        // Brand color #EAA032
            wxColour orange_hover(244, 180, 80);               // Lighter hover state
            wxColour text_color = *wxBLACK;                    // Black text for contrast on tan background
            // Disabled colors use centralized UIColors (matching tab disabled style)
            wxColour disabled_bg = UIColors::TabBackgroundDisabled();
            wxColour disabled_text = UIColors::TabTextDisabled();
            const int corner_radius = GetScaledSmallCornerRadius();
            const int dropdown_width = m_show_dropdown ? GetScaledDropdownWidth()
                                                       : 0; // Only show dropdown area if needed

            // First, clear the entire button area with the parent background color
            dc.SetBrush(wxBrush(GetBackgroundColour()));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, size.x, size.y);

            // Choose colors based on enabled state (both Plater and tab conditions must be met)
            bool effectively_enabled = m_slice_button_enabled && m_slice_tab_enabled;
            wxColour bg_color = effectively_enabled ? dark_bg : disabled_bg;
            wxColour border_color = effectively_enabled ? orange : disabled_bg;
            wxColour current_text_color = effectively_enabled ? text_color : disabled_text;

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

                // Draw dropdown arrow (chevron) - all dimensions scaled for DPI
                const int chevron_pen = GetScaledChevronPenWidth();
                const int chevron_offset = GetScaledChevronOffset();
                const int chevron_size = GetScaledChevronArrowSize();
                dc.SetPen(wxPen(*wxWHITE, chevron_pen));
                int arrow_x = dropdown_width / 2;
                int arrow_y = size.y / 2 - chevron_offset;
                dc.DrawLine(arrow_x - chevron_size, arrow_y, arrow_x, arrow_y + chevron_size);
                dc.DrawLine(arrow_x, arrow_y + chevron_size, arrow_x + chevron_size, arrow_y);
            }

            // Draw thin border around entire button
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(border_color, 1));
            dc.DrawRoundedRectangle(0, 0, size.x - 1, size.y - 1, corner_radius);

            // Draw text
            dc.SetTextForeground(current_text_color);
            dc.SetFont(GetFont());
            wxString label = m_has_sliced_object ? _L("Export G-code") : _L("Slice Platter");
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
                             if (!m_slice_button_enabled || !m_slice_tab_enabled)
                                 return;

                             m_slice_button_pressed = true;
                             m_slice_button->Refresh();

                             if (m_show_dropdown)
                             {
                                 // Connected to printer and sliced - show dropdown anywhere on button
                                 // Clicked on dropdown area - show menu
                                 wxMenu menu;
                                 const int ID_SAVE_LOCALLY = wxID_HIGHEST + 1;
                                 const int ID_SEND_TO_PRINTER = wxID_HIGHEST + 2;
                                 wxMenuItem *save_item = menu.Append(ID_SAVE_LOCALLY, _L("Save locally"));
                                 save_item->SetBitmap(*get_bmp_bundle("save"));
                                 wxMenuItem *send_item = menu.Append(ID_SEND_TO_PRINTER, _L("Send to Printer"));
                                 send_item->SetBitmap(*get_bmp_bundle("export_gcode"));

                                 // Show menu at button position using CustomMenu for theming
                                 wxPoint menu_pos = m_slice_button->GetPosition();
                                 menu_pos.y += m_slice_button->GetSize().y;
                                 auto customMenu = CustomMenu::FromWxMenu(&menu, this);
                                 if (customMenu)
                                 {
                                     // Override FromWxMenu callbacks with direct lambdas so
                                     // we don't route through the stack-local wxMenu (which
                                     // is destroyed once ShowAt returns asynchronously).
                                     customMenu->SetCallback(ID_SAVE_LOCALLY,
                                                             [this]()
                                                             {
                                                                 if (m_export_callback)
                                                                     m_export_callback();
                                                             });
                                     customMenu->SetCallback(ID_SEND_TO_PRINTER,
                                                             [this]()
                                                             {
                                                                 if (m_send_to_printer_callback)
                                                                     m_send_to_printer_callback();
                                                             });
                                     customMenu->KeepAliveUntilDismissed(customMenu);
                                     if (!customMenu->GetParent())
                                         customMenu->Create(this);
                                     wxPoint screenPos = ClientToScreen(menu_pos);
                                     customMenu->ShowAt(screenPos, this);
                                 }
                                 else
                                 {
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
                                     PopupMenu(&menu, menu_pos);
                                 }
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
                             if (!m_slice_button_enabled || !m_slice_tab_enabled)
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
    GetSizer()->Add(m_slice_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, GetScaledMargin());
}

void ModernTabBar::UpdateSliceButtonState(bool has_sliced_object)
{
    if (!m_slice_button)
        return;

    m_has_sliced_object = has_sliced_object;

    // Update dropdown visibility: only show in Export mode when printer is online
    m_show_dropdown = has_sliced_object && IsPrinterConnected() &&
                      m_connection_state == PrinterConnectionChecker::State::Online;

    // Update button visibility based on current tab
    UpdateSliceButtonVisibility();

    m_slice_button->SetLabel(has_sliced_object ? _L("Export G-code") : _L("Slice Platter"));
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

    // Always visible regardless of tab
    m_slice_button->Show();

    // In Slice mode: only actionable on Prepare tab
    // In Export mode: actionable on all tabs
    m_slice_tab_enabled = m_has_sliced_object || (m_selected_tab == TAB_PREPARE);

    m_slice_button->Refresh();
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

    // Re-evaluate dropdown visibility based on current printer connection (must be online)
    bool was_showing_dropdown = m_show_dropdown;
    m_show_dropdown = m_has_sliced_object && IsPrinterConnected() &&
                      m_connection_state == PrinterConnectionChecker::State::Online;

    // Only refresh if state changed
    if (was_showing_dropdown != m_show_dropdown)
    {
        m_slice_button->Refresh();
    }
}

void ModernTabBar::ShowPrinterWebViewTab(const wxString &printerName, std::function<void()> callback)
{
    // Remove existing tab if any
    HidePrinterWebViewTab();

    m_printer_webview_name = printerName;
    m_printer_webview_callback = callback;

    // Create a custom panel for the printer webview tab with status indicator
    // Width matches regular tab buttons (scaled)
    m_printer_webview_btn = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_printer_webview_btn->SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Size the button to fit the printer name (with dot + margins), minimum is the standard button width
    {
        wxClientDC dc(m_printer_webview_btn);
        dc.SetFont(m_printer_webview_btn->GetFont());
        wxCoord tw = 0, th = 0;
        dc.GetTextExtent(printerName, &tw, &th);
        int needed_w = 2 * GetScaledHMargin() + GetScaledDotSize() + GetScaledDotTextGap() + tw;
        int btn_w = std::max(needed_w, GetScaledButtonWidth());
        wxSize btn_sz(btn_w, GetScaledButtonHeight());
        m_printer_webview_btn->SetMinSize(btn_sz);
        m_printer_webview_btn->SetSize(btn_sz);
    }

    // Set initial background color to match theme (important for proper initial rendering)
    m_printer_webview_btn->SetBackgroundColour(m_color_bg_normal);

    // Custom paint to draw status dot and label
    // Capture button pointer directly (like regular buttons) for stable reference during paint
    wxPanel *btn = m_printer_webview_btn;
    btn->Bind(wxEVT_PAINT,
              [btn, this](wxPaintEvent &evt)
              {
                  wxAutoBufferedPaintDC dc(btn);
                  wxSize size = btn->GetSize();

                  bool is_active = (m_selected_tab == TAB_PRINTER_WEBVIEW);
                  const int corner_radius = GetScaledCornerRadius();
                  const int dot_size = GetScaledDotSize();

                  // First, fill entire area with parent background color
                  dc.SetPen(*wxTRANSPARENT_PEN);
                  dc.SetBrush(wxBrush(GetBackgroundColour()));
                  dc.DrawRectangle(0, 0, size.x, size.y);

                  // Draw the rounded button background (use GetBackgroundColour for hover support)
                  dc.SetBrush(wxBrush(btn->GetBackgroundColour()));
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

                  // Draw status indicator dot
                  wxColour dot_color;
                  switch (m_connection_state)
                  {
                  case PrinterConnectionChecker::State::Online:
                      dot_color = wxColour(46, 184, 46); // Green
                      break;
                  case PrinterConnectionChecker::State::Offline:
                      dot_color = wxColour(220, 60, 60); // Red
                      break;
                  default:
                      dot_color = wxColour(128, 128, 128); // Gray
                      break;
                  }

                  // Draw text setup
                  dc.SetTextForeground(is_active ? m_color_text_selected : m_color_text_normal);
                  dc.SetFont(btn->GetFont());

                  wxCoord textWidth, textHeight;
                  dc.GetTextExtent(m_printer_webview_name, &textWidth, &textHeight);

                  // Calculate available width and truncate text if needed
                  const int dot_text_gap = GetScaledDotTextGap();
                  const int h_margin = GetScaledHMargin();
                  int max_content_width = size.x - 2 * h_margin;
                  int available_text_width = max_content_width - dot_size - dot_text_gap;

                  wxString display_text = m_printer_webview_name;
                  if (textWidth > available_text_width)
                  {
                      while (textWidth > available_text_width && display_text.length() > 3)
                      {
                          display_text = display_text.Left(display_text.length() - 4) + "...";
                          dc.GetTextExtent(display_text, &textWidth, &textHeight);
                      }
                  }

                  // Center the dot+text group horizontally
                  int content_width = dot_size + dot_text_gap + textWidth;
                  int content_x = (size.x - content_width) / 2;

                  // Draw status indicator dot (centered with text)
                  dc.SetBrush(wxBrush(dot_color));
                  dc.SetPen(*wxTRANSPARENT_PEN);
                  int dot_y = (size.y - dot_size) / 2;
                  dc.DrawEllipse(content_x, dot_y, dot_size, dot_size);

                  // Draw text (after dot)
                  int text_x = content_x + dot_size + dot_text_gap;
                  int text_y = (size.y - textHeight) / 2;
                  dc.DrawText(display_text, text_x, text_y);
              });

    // Handle click
    m_printer_webview_btn->Bind(wxEVT_LEFT_UP,
                                [this](wxMouseEvent &)
                                {
                                    m_selected_tab = TAB_PRINTER_WEBVIEW;
                                    UpdateButtonStyles();
                                    UpdateSliceButtonVisibility();
                                    if (m_printer_webview_callback)
                                    {
                                        m_printer_webview_callback();
                                    }
                                    m_printer_webview_btn->Refresh();
                                });

    // Handle hover
    m_printer_webview_btn->Bind(wxEVT_ENTER_WINDOW,
                                [this](wxMouseEvent &)
                                {
                                    m_printer_webview_btn->SetBackgroundColour(m_color_bg_hover);
                                    m_printer_webview_btn->Refresh();

                                    // Set tooltip based on connection state
                                    wxString tooltip;
                                    switch (m_connection_state)
                                    {
                                    case PrinterConnectionChecker::State::Online:
                                        tooltip = _L("Printer is online - Click to open web interface");
                                        break;
                                    case PrinterConnectionChecker::State::Offline:
                                        tooltip = _L("Printer is offline");
                                        break;
                                    default:
                                        tooltip = _L("Checking printer connection...");
                                        break;
                                    }
                                    m_printer_webview_btn->SetToolTip(tooltip);
                                });

    m_printer_webview_btn->Bind(wxEVT_LEAVE_WINDOW,
                                [this](wxMouseEvent &)
                                {
                                    m_printer_webview_btn->SetBackgroundColour((m_selected_tab == TAB_PRINTER_WEBVIEW)
                                                                                   ? m_color_bg_selected
                                                                                   : m_color_bg_normal);
                                    m_printer_webview_btn->Refresh();
                                });

    // Insert the button after Settings dropdown, before the stretch spacer
    wxSizer *sizer = GetSizer();
    // The sizer structure is: spacer(0), tabs(1..n), settings_dropdown, stretch_spacer, slice_button
    // Count: initial spacer + tabs + settings_dropdown = 1 + m_tabs.size() + 1
    // We want to insert after settings_dropdown, before stretch spacer
    // Sizer structure: spacer(0), tabs(1..n), 3 individual settings btns, search btn, settings dropdown, [webview here]
    int settings_btn_count = (m_print_settings_btn ? 3 : 0) + (m_search_btn ? 1 : 0) +
                             (m_settings_dropdown_btn ? 1 : 0);
    m_printer_webview_sizer_index = 1 + static_cast<int>(m_tabs.size()) + settings_btn_count;

    sizer->Insert(m_printer_webview_sizer_index, m_printer_webview_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                  GetScaledSmallMargin());

    // Re-evaluate settings layout since printer button width affects available space
    UpdateSettingsLayout(true);
}

void ModernTabBar::HidePrinterWebViewTab()
{
    if (!m_printer_webview_btn)
        return;

    // Stop connection checker
    if (m_connection_checker)
    {
        m_connection_checker->StopPolling();
        m_connection_checker.reset();
    }

    // If printer webview was selected, select Prepare tab instead
    if (m_selected_tab == TAB_PRINTER_WEBVIEW)
    {
        SelectTab(TAB_PREPARE);
    }

    // Remove button from sizer and destroy
    wxSizer *sizer = GetSizer();
    sizer->Detach(m_printer_webview_btn);
    m_printer_webview_btn->Destroy();
    m_printer_webview_btn = nullptr;
    m_printer_webview_sizer_index = -1;
    m_connection_state = PrinterConnectionChecker::State::Unknown;

    // Re-evaluate settings layout since printer button removal frees space
    UpdateSettingsLayout(true);
}

void ModernTabBar::UpdatePrinterConnectionState(PrinterConnectionChecker::State state)
{
    if (m_connection_state == state)
        return;

    m_connection_state = state;

    if (m_printer_webview_btn)
    {
        m_printer_webview_btn->Refresh();
    }

    // Update slice button dropdown visibility based on new connection state
    if (m_slice_button)
    {
        bool was_showing_dropdown = m_show_dropdown;
        m_show_dropdown = m_has_sliced_object && IsPrinterConnected() &&
                          m_connection_state == PrinterConnectionChecker::State::Online;
        if (was_showing_dropdown != m_show_dropdown)
        {
            m_slice_button->Refresh();
        }
    }
}

void ModernTabBar::SelectPrinterWebViewTab()
{
    if (!m_printer_webview_btn)
        return;

    m_selected_tab = TAB_PRINTER_WEBVIEW;
    UpdateButtonStyles();
    UpdateSliceButtonVisibility();

    if (m_printer_webview_callback)
    {
        m_printer_webview_callback();
    }

    m_printer_webview_btn->Refresh();
}

void ModernTabBar::SetPrinterConfig(const DynamicPrintConfig *config)
{
    // Create connection checker if needed
    if (!m_connection_checker)
    {
        m_connection_checker = std::make_unique<PrinterConnectionChecker>([this](PrinterConnectionChecker::State state)
                                                                          { UpdatePrinterConnectionState(state); });
    }

    m_connection_checker->SetPrinterConfig(config);

    // Start polling if we have a printer webview tab
    if (m_printer_webview_btn && config)
    {
        m_connection_checker->StartPolling(20000); // 20 seconds
    }
    else
    {
        m_connection_checker->StopPolling();
    }
}

} // namespace GUI
} // namespace Slic3r
