///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/panel.h>
#include <wx/button.h>
#include <vector>
#include <functional>

namespace Slic3r
{
namespace GUI
{

class MainFrame;

class ModernTabBar : public wxPanel
{
public:
    ModernTabBar(wxWindow *parent);
    ~ModernTabBar() = default;

    // Tab types
    enum TabType
    {
        TAB_PREPARE = 0,
        TAB_PREVIEW,
        TAB_PRINT_SETTINGS,
        TAB_FILAMENTS,
        TAB_PRINTERS,
        TAB_COUNT
    };

    // Add a button/tab
    void AddButton(TabType type, const wxString &label, std::function<void()> callback);

    // Select a tab programmatically
    void SelectTab(TabType type);

    // Enable/disable tabs
    void EnableTab(TabType type, bool enable = true);

    // Check if a tab is selected
    bool IsSelected(TabType type) const { return m_selected_tab == type; }

    void AddSliceButton(std::function<void()> slice_callback, std::function<void()> export_callback);
    void UpdateSliceButtonState(bool has_sliced_object);
    void HideSliceButton();
    void ShowSliceButton();

    void UpdateSliceButtonVisibility();  // Updates visibility based on current tab
    void EnableSliceButton(bool enable); // Enable/disable based on platter contents
    bool IsPrinterConnected() const;     // Check if physical printer with print_host is configured
    void SetSendToPrinterCallback(std::function<void()> callback); // Set callback for Send to Printer
    void RefreshPrinterConnectionState();                          // Re-evaluate printer connection and update dropdown

    void sys_color_changed();

private:
    struct TabButton
    {
        wxButton *button;
        TabType type;
        std::function<void()> callback;
        bool enabled;
    };

    void OnButtonClick(TabType type);
    void UpdateButtonStyles();
    wxButton *CreateStyledButton(const wxString &label);

    void UpdateColors();

    std::vector<TabButton> m_tabs;
    TabType m_selected_tab{TAB_PREPARE};

    wxButton *m_slice_button{nullptr};
    std::function<void()> m_slice_callback;
    std::function<void()> m_export_callback;
    std::function<void()> m_send_to_printer_callback;
    bool m_has_sliced_object{false};
    bool m_slice_button_pressed{false};
    bool m_slice_button_enabled{true};
    bool m_show_dropdown{false}; // Dropdown only shown in Export mode when printer connected

    wxColour m_color_bg_normal;
    wxColour m_color_bg_hover;
    wxColour m_color_bg_selected;
    wxColour m_color_text_normal;
    wxColour m_color_text_selected;
    wxColour m_color_text_disabled;
    wxColour m_color_border;
};

} // namespace GUI
} // namespace Slic3r
