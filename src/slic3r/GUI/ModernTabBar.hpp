///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/panel.h>
#include <vector>
#include <functional>
#include <memory>

#include "PrinterConnectionChecker.hpp"

namespace Slic3r
{

class DynamicPrintConfig;

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
        TAB_PRINTER_WEBVIEW, // Dynamic printer interface tab
        TAB_COUNT
    };

    // Add a button/tab
    void AddButton(TabType type, const wxString &label, std::function<void()> callback);

    // Add Settings dropdown button (replaces individual Print Settings, Filaments, Printers buttons)
    void AddSettingsDropdownButton(std::function<void(TabType)> callback);

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

    // Printer webview tab methods
    void ShowPrinterWebViewTab(const wxString &printerName, std::function<void()> callback);
    void HidePrinterWebViewTab();
    void UpdatePrinterConnectionState(PrinterConnectionChecker::State state);
    bool HasPrinterWebViewTab() const { return m_printer_webview_btn != nullptr; }
    void SelectPrinterWebViewTab();
    void SetPrinterConfig(const DynamicPrintConfig *config);

    void sys_color_changed();
    void msw_rescale();

private:
    struct TabButton
    {
        wxPanel *button;
        TabType type;
        std::function<void()> callback;
        bool enabled;
    };

    void OnButtonClick(TabType type);
    void UpdateButtonStyles();
    wxPanel *CreateStyledButton(const wxString &label);

    void UpdateColors();

    std::vector<TabButton> m_tabs;
    TabType m_selected_tab{TAB_PREPARE};

    wxPanel *m_slice_button{nullptr};
    std::function<void()> m_slice_callback;
    std::function<void()> m_export_callback;
    std::function<void()> m_send_to_printer_callback;
    bool m_has_sliced_object{false};
    bool m_slice_button_pressed{false};
    bool m_slice_button_enabled{true};
    bool m_slice_tab_enabled{true}; // Whether current tab allows slicing
    bool m_show_dropdown{false};    // Dropdown only shown in Export mode when printer connected

    wxColour m_color_bg_normal;
    wxColour m_color_bg_hover;
    wxColour m_color_bg_selected;
    wxColour m_color_text_normal;
    wxColour m_color_text_selected;
    wxColour m_color_text_disabled;
    wxColour m_color_border;

    // Settings dropdown button (shown when collapsed)
    wxPanel *m_settings_dropdown_btn{nullptr};
    std::function<void(TabType)> m_settings_callback;

    // Individual settings buttons (shown when expanded)
    wxPanel *m_print_settings_btn{nullptr};
    wxPanel *m_filament_settings_btn{nullptr};
    wxPanel *m_printer_settings_btn{nullptr};
    wxPanel *m_search_btn{nullptr};
    bool m_settings_expanded{false};

    void UpdateSettingsLayout(bool force = false);

    // Printer webview tab members
    wxPanel *m_printer_webview_btn{nullptr};
    std::unique_ptr<PrinterConnectionChecker> m_connection_checker;
    PrinterConnectionChecker::State m_connection_state{PrinterConnectionChecker::State::Unknown};
    wxString m_printer_webview_name;
    std::function<void()> m_printer_webview_callback;
    int m_printer_webview_sizer_index{-1}; // Index in sizer for insertion/removal
};

} // namespace GUI
} // namespace Slic3r
