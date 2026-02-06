///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Sidebar.hpp"
#include "libslic3r/AppConfig.hpp"
#include "Widgets/CollapsibleSection.hpp"
#include "ConfigManipulation.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "GUI_ObjectSettings.hpp"
#include "GUI_ObjectLayers.hpp"
#include "Plater.hpp"
#include "PresetComboBoxes.hpp"
#include "wxExtensions.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "MainFrame.hpp"
#include "Tab.hpp"
#include "BedShapeDialog.hpp"
#include "WipeTowerDialog.hpp"
#include "PhysicalPrinterDialog.hpp"
#include "MsgDialog.hpp"

#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/button.h>
#include <wx/settings.h>
#include <wx/statbox.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include "Widgets/SpinInput.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/ThemedTextCtrl.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ScrollablePanel.hpp"
#include "Widgets/UIColors.hpp"
#include "Widgets/FlatStaticBox.hpp"

#ifdef _WIN32
#include "DarkMode.hpp"
#include <uxtheme.h>
#include <commctrl.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")
#endif
#include <functional>
#include <wx/statbmp.h>
#include <wx/colordlg.h>
#include <wx/clrpicker.h>

#include "libslic3r/GCode/Thumbnails.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelProcessing.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <boost/algorithm/string.hpp>

using Slic3r::GUI::format_wxstr;

#ifdef _WIN32
// Subclass procedure to draw flat borders on wxStaticBox in light mode
static LRESULT CALLBACK FlatBorderSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass,
                                               DWORD_PTR dwRefData)
{
    // Call default handler first
    LRESULT result = DefSubclassProc(hwnd, uMsg, wParam, lParam);

    // After paint, draw our flat border on top
    if (uMsg == WM_PAINT)
    {
        HDC hdc = ::GetDC(hwnd);
        RECT rc;
        ::GetClientRect(hwnd, &rc);

        // Draw flat border using centralized UIColors
        HBRUSH borderBrush = CreateSolidBrush(UIColorsWin::StaticBoxBorder());
        ::FrameRect(hdc, &rc, borderBrush);
        DeleteObject(borderBrush);

        ::ReleaseDC(hwnd, hdc);
    }

    return result;
}
#endif

namespace Slic3r
{
namespace GUI
{

// ============================================================================
// Theme color constants for sidebar UI
// ============================================================================
namespace SidebarColors
{
// All colors delegated to UIColors for centralized theming

// Building blocks (Dark/Light specific) - kept for backward compatibility
inline wxColour DarkBackground()
{
    return UIColors::PanelBackgroundDark();
}
inline wxColour DarkForeground()
{
    return UIColors::PanelForegroundDark();
}
inline wxColour DarkInputBackground()
{
    return UIColors::InputBackgroundDark();
}
inline wxColour DarkInputForeground()
{
    return UIColors::InputForegroundDark();
}
inline wxColour DarkDisabledBackground()
{
    return UIColors::InputBackgroundDisabledDark();
}
inline wxColour DarkDisabledForeground()
{
    return UIColors::InputForegroundDisabledDark();
}

inline wxColour LightBackground()
{
    return UIColors::ContentBackgroundLight();
}
inline wxColour LightForeground()
{
    return UIColors::PanelForegroundLight();
}
inline wxColour LightInputBackground()
{
    return UIColors::InputBackgroundLight();
}
inline wxColour LightInputForeground()
{
    return UIColors::InputForegroundLight();
}
inline wxColour LightDisabledBackground()
{
    return UIColors::InputBackgroundDisabledLight();
}
inline wxColour LightDisabledForeground()
{
    return UIColors::InputForegroundDisabledLight();
}

// Unified accessors - USE THESE! No dark_mode() checks needed by callers.
inline wxColour Background()
{
    return UIColors::ContentBackground();
}
inline wxColour Foreground()
{
    return UIColors::ContentForeground();
}
inline wxColour InputBackground()
{
    return UIColors::InputBackground();
}
inline wxColour InputForeground()
{
    return UIColors::InputForeground();
}
inline wxColour DisabledBackground()
{
    return UIColors::InputBackgroundDisabled();
}
inline wxColour DisabledForeground()
{
    return UIColors::InputForegroundDisabled();
}
} // namespace SidebarColors

// ============================================================================
// Helper to create wxStaticBoxSizer with FlatStaticBox for proper flat borders
// ============================================================================
static wxStaticBoxSizer *CreateFlatStaticBoxSizer(wxWindow *parent, const wxString &label, int orient = wxVERTICAL)
{
    auto *stb = new FlatStaticBox(parent, wxID_ANY, label);
    stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
    stb->SetFont(wxGetApp().bold_font());
    wxGetApp().UpdateDarkUI(stb);
    return new wxStaticBoxSizer(stb, orient);
}

// ============================================================================
// DPI-scaled sizes for consistent UI scaling
// ============================================================================

// Icon size for lock/undo icons (16px at default em=10)
static int GetScaledIconSize()
{
    return int(1.6 * wxGetApp().em_unit());
}

static wxSize GetScaledIconSizeWx()
{
    int size = GetScaledIconSize();
    return wxSize(size, size);
}

// Standard input control width (70px at default em=10)
static int GetScaledInputWidth()
{
    return int(7 * wxGetApp().em_unit());
}

// Small input control width for coordinates (40px at default em=10)
static int GetScaledSmallInputWidth()
{
    return int(4 * wxGetApp().em_unit());
}

// Icon margin spacing (2px at default em=10)
static int GetIconMargin()
{
    return wxGetApp().em_unit() / 5;
}

// Check if any setting in the list is visible in sidebar
// Also checks indexed variants (key#0, key#1, etc.) for extruder-specific settings
static bool has_any_visible_setting(std::initializer_list<const char *> opt_keys)
{
    for (const char *key : opt_keys)
    {
        // Check base key first
        std::string visibility = get_app_config()->get("sidebar_visibility", key);
        if (visibility != "0")
        {
            // If base key is not explicitly hidden AND no indexed keys exist, it's visible
            // Check if any indexed variant exists
            bool has_indexed = false;
            for (int i = 0; i < 16; ++i) // Support up to 16 extruders
            {
                std::string indexed_key = std::string(key) + "#" + std::to_string(i);
                std::string indexed_vis = get_app_config()->get("sidebar_visibility", indexed_key);
                if (!indexed_vis.empty())
                {
                    has_indexed = true;
                    if (indexed_vis != "0")
                        return true;
                }
            }
            // If no indexed keys exist, use base key visibility (empty = visible by default)
            if (!has_indexed)
                return true;
        }
    }
    return false;
}

// Check if any of the given settings are visible for a specific extruder index
static bool has_extruder_visible_setting(std::initializer_list<const char *> opt_keys, size_t extruder_idx)
{
    for (const char *key : opt_keys)
    {
        std::string indexed_key = std::string(key) + "#" + std::to_string(extruder_idx);
        std::string visibility = get_app_config()->get("sidebar_visibility", indexed_key);
        // Empty means visible by default, "0" means hidden
        if (visibility != "0")
            return true;
    }
    return false;
}

// Check if any option in the given categories is visible in sidebar
// This dynamically queries print_config_def instead of using hardcoded option lists
// Options with sidebar checkboxes are initialized to "1" on first render, so:
//   - "1" = visible (default or explicitly enabled)
//   - "0" = explicitly hidden by user
//   - empty = no checkbox exists for this option
static bool is_any_category_visible(std::initializer_list<const char *> categories)
{
    bool found_any_tracked_option = false;

    for (const auto &[opt_key, opt_def] : print_config_def.options)
    {
        // Check if this option's category matches any of the requested categories
        bool category_match = false;
        for (const char *cat : categories)
        {
            if (opt_def.category == cat)
            {
                category_match = true;
                break;
            }
        }
        if (!category_match)
            continue;

        // Check indexed variants first (e.g., opt_key#0, opt_key#1 for extruder-specific options)
        for (int i = 0; i < 16; ++i)
        {
            std::string indexed_key = opt_key + "#" + std::to_string(i);
            std::string indexed_vis = get_app_config()->get("sidebar_visibility", indexed_key);
            if (!indexed_vis.empty())
            {
                found_any_tracked_option = true;
                if (indexed_vis != "0")
                    return true; // Found a visible indexed option
            }
        }

        // Check base key
        std::string visibility = get_app_config()->get("sidebar_visibility", opt_key);
        if (!visibility.empty())
        {
            found_any_tracked_option = true;
            if (visibility != "0")
                return true; // Found a visible option
        }
    }

    // If no options in these categories have visibility tracking,
    // show the tab by default (user hasn't opened Tab settings yet)
    return !found_any_tracked_option;
}

// ============================================================================
// RAII guard for m_disable_update flag - prevents flag from getting stuck
// ============================================================================
class DisableUpdateGuard
{
    bool &m_flag;
    bool m_previous_value;

public:
    explicit DisableUpdateGuard(bool &flag) : m_flag(flag), m_previous_value(flag) { m_flag = true; }
    ~DisableUpdateGuard() { m_flag = m_previous_value; }

    // Non-copyable
    DisableUpdateGuard(const DisableUpdateGuard &) = delete;
    DisableUpdateGuard &operator=(const DisableUpdateGuard &) = delete;
};

#ifdef _WIN32
// Callback for EnumChildWindows to apply appropriate theme to child windows
static BOOL CALLBACK ApplyDarkThemeToChildWindows(HWND hwnd, LPARAM)
{
    // Get window class name to determine handling
    wchar_t className[256];
    GetClassNameW(hwnd, className, 256);

    // Edit controls need visual styles DISABLED for SetBackgroundColour to work
    if (wcscmp(className, L"Edit") == 0)
    {
        SetWindowTheme(hwnd, L"", L"");
    }
    else
    {
        // Other controls can use DarkMode_Explorer for scrollbars etc.
        NppDarkMode::SetDarkExplorerTheme(hwnd);
    }
    return TRUE;
}
#endif

// Helper function to recursively apply theme colors to all controls
// Uses unified SidebarColors accessors - no dark_mode() checks needed
static void ApplyDarkModeToStaticBoxes(wxWindow *window)
{
    if (!window)
        return;

    // Get colors from unified accessors - these automatically return correct colors for current theme
    wxColour panel_bg = SidebarColors::Background();
    wxColour panel_fg = SidebarColors::Foreground();
    wxColour input_bg = SidebarColors::InputBackground();
    wxColour input_fg = SidebarColors::InputForeground();

    // Apply to static boxes
    if (wxStaticBox *static_box = dynamic_cast<wxStaticBox *>(window))
    {
        wxGetApp().UpdateDarkUI(static_box);
        static_box->SetBackgroundColour(panel_bg);
        static_box->SetForegroundColour(panel_fg);
        // Update FlatStaticBox theme for proper flat borders
        if (auto *flat_stb = dynamic_cast<FlatStaticBox *>(static_box))
            flat_stb->SysColorsChanged();
        else
            static_box->Refresh();
    }
    // Apply to labels
    else if (wxStaticText *label = dynamic_cast<wxStaticText *>(window))
    {
        label->SetForegroundColour(panel_fg);
        label->SetBackgroundColour(panel_bg);
        label->Refresh();
    }
    // Apply to static bitmaps (lock/undo icons)
    else if (wxStaticBitmap *bitmap = dynamic_cast<wxStaticBitmap *>(window))
    {
        bitmap->SetBackgroundColour(panel_bg);
        bitmap->Refresh();
    }
    // Apply to panels
    else if (wxPanel *panel = dynamic_cast<wxPanel *>(window))
    {
        panel->SetBackgroundColour(panel_bg);
        panel->SetForegroundColour(panel_fg);
#ifdef _WIN32
        wxGetApp().UpdateDarkUI(panel);
#endif
    }
    // Apply to text controls
    else if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(window))
    {
        // Check if this is a ThemedTextCtrl (used by TextInput, SpinInput, ComboBox)
        bool is_themed = dynamic_cast<Slic3r::GUI::ThemedTextCtrl *>(text) != nullptr;

        if (is_themed)
        {
            // ThemedTextCtrl handles its own theming via parent widget's SysColorsChanged()
#ifdef _WIN32
            RedrawWindow(text->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
#endif
            text->Refresh();
        }
        else
        {
            // Regular wxTextCtrl - apply colors directly
#ifdef _WIN32
            SetWindowTheme(text->GetHWND(), L"", L"");
            bool is_editable = text->IsEditable();
            text->SetBackgroundColour(is_editable ? input_bg : SidebarColors::DisabledBackground());
            text->SetForegroundColour(is_editable ? input_fg : SidebarColors::DisabledForeground());
            RedrawWindow(text->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
#else
            bool is_enabled = text->IsEnabled();
            text->SetBackgroundColour(is_enabled ? input_bg : SidebarColors::DisabledBackground());
            text->SetForegroundColour(is_enabled ? input_fg : SidebarColors::DisabledForeground());
#endif
            text->Refresh();
        }
    }
    // Apply to SpinInput controls (custom themed spin controls)
    else if (SpinInput *spin = dynamic_cast<SpinInput *>(window))
    {
        spin->SysColorsChanged();
        spin->Refresh();
    }
    // Apply to custom ComboBox widgets
    else if (::ComboBox *combo = dynamic_cast<::ComboBox *>(window))
    {
        combo->SysColorsChanged();
        combo->Refresh();
    }
    // Apply to TextInput controls
    else if (TextInput *text_input = dynamic_cast<TextInput *>(window))
    {
        text_input->SysColorsChanged();
        text_input->Refresh();
    }
    // Apply to native wxSpinCtrl controls
    else if (wxSpinCtrl *spin = dynamic_cast<wxSpinCtrl *>(window))
    {
        wxGetApp().UpdateDarkUI(spin);
#ifdef _WIN32
        NppDarkMode::SetDarkExplorerTheme(spin->GetHWND());
        EnumChildWindows(spin->GetHWND(), ApplyDarkThemeToChildWindows, 0);
#endif
        spin->SetBackgroundColour(input_bg);
        spin->SetForegroundColour(input_fg);
        spin->Refresh();
    }
    // Apply to native wxComboBox
    else if (wxComboBox *combo = dynamic_cast<wxComboBox *>(window))
    {
        wxGetApp().UpdateDarkUI(combo);
#ifdef _WIN32
        NppDarkMode::SetDarkExplorerTheme(combo->GetHWND());
        EnumChildWindows(combo->GetHWND(), ApplyDarkThemeToChildWindows, 0);
#endif
        combo->SetBackgroundColour(input_bg);
        combo->SetForegroundColour(input_fg);
        combo->Refresh();
    }
    // Apply to choice controls (dropdowns)
    else if (wxChoice *choice = dynamic_cast<wxChoice *>(window))
    {
        wxGetApp().UpdateDarkUI(choice);
#ifdef _WIN32
        NppDarkMode::SetDarkExplorerTheme(choice->GetHWND());
        EnumChildWindows(choice->GetHWND(), ApplyDarkThemeToChildWindows, 0);
#endif
        choice->SetBackgroundColour(input_bg);
        choice->SetForegroundColour(input_fg);
        choice->Refresh();
    }
    // Apply to custom checkboxes
    else if (::CheckBox *checkbox = dynamic_cast<::CheckBox *>(window))
    {
        checkbox->sys_color_changed();
        checkbox->SetForegroundColour(panel_fg);
        checkbox->Refresh();
    }
    // Apply to ScalableButtons
    else if (ScalableButton *btn = dynamic_cast<ScalableButton *>(window))
    {
        btn->sys_color_changed();
    }
    // Apply to regular wxButton
    else if (wxButton *btn = dynamic_cast<wxButton *>(window))
    {
        wxGetApp().UpdateDarkUI(btn);
#ifdef _WIN32
        NppDarkMode::SetDarkExplorerTheme(btn->GetHWND());
#endif
        btn->Refresh();
    }

    // Recursively process children
    for (wxWindow *child : window->GetChildren())
    {
        ApplyDarkModeToStaticBoxes(child);
    }
}

// ============================================================================
// ObjectInfo - Display object size, volume, facets and mesh status
// ============================================================================

class ObjectInfo : public wxStaticBoxSizer
{
    std::string m_warning_icon_name{"exclamation"};

public:
    ObjectInfo(wxWindow *parent);

    wxStaticBitmap *manifold_warning_icon;
    wxStaticBitmap *info_icon;
    wxStaticText *info_size;
    wxStaticText *info_volume;
    wxStaticText *info_facets;
    wxStaticText *info_manifold;

    wxStaticText *label_volume;
    std::vector<wxStaticText *> sla_hidden_items;

    bool showing_manifold_warning_icon;
    void show_sizer(bool show);
    void update_warning_icon(const std::string &warning_icon_name);
    void sys_color_changed();
};

ObjectInfo::ObjectInfo(wxWindow *parent) : wxStaticBoxSizer(new FlatStaticBox(parent, wxID_ANY, _L("Info")), wxVERTICAL)
{
    GetStaticBox()->SetBackgroundStyle(wxBG_STYLE_PAINT);
    GetStaticBox()->SetFont(wxGetApp().bold_font());
    wxGetApp().UpdateDarkUI(GetStaticBox());

#ifdef _WIN32
    // Use unified color accessor - no dark_mode() check needed
    wxColour label_color = SidebarColors::Foreground();
#endif

    int em = wxGetApp().em_unit();
    auto *grid_sizer = new wxFlexGridSizer(4, em / 2, int(1.5 * em));
    grid_sizer->SetFlexibleDirection(wxHORIZONTAL);

    auto init_info_label = [parent, grid_sizer
#ifdef _WIN32
                            ,
                            label_color
#endif
    ](wxStaticText **info_label, wxString text_label, wxSizer *sizer_with_icon = nullptr)
    {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label + ":");
        text->SetFont(wxGetApp().small_font());
#ifdef _WIN32
        text->SetForegroundColour(label_color);
#endif
        *info_label = new wxStaticText(parent, wxID_ANY, "");
        (*info_label)->SetFont(wxGetApp().small_font());
#ifdef _WIN32
        (*info_label)->SetForegroundColour(label_color);
#endif
        grid_sizer->Add(text, 0);
        if (sizer_with_icon)
        {
            sizer_with_icon->Insert(0, *info_label, 0);
            grid_sizer->Add(sizer_with_icon, 0, wxEXPAND);
        }
        else
            grid_sizer->Add(*info_label, 0);
        return text;
    };

    init_info_label(&info_size, _L("Size"));

    info_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("info"));
    info_icon->SetToolTip(_L("For a multipart object, this value isn't accurate.\n"
                             "It doesn't take account of intersections and negative volumes."));
    auto *volume_info_sizer = new wxBoxSizer(wxHORIZONTAL);
    volume_info_sizer->Add(info_icon, 0, wxLEFT, em);
    label_volume = init_info_label(&info_volume, _L("Volume"), volume_info_sizer);

    init_info_label(&info_facets, _L("Facets"));
    Add(grid_sizer, 0, wxEXPAND);

    info_manifold = new wxStaticText(parent, wxID_ANY, "");
    info_manifold->SetFont(wxGetApp().small_font());
#ifdef _WIN32
    info_manifold->SetForegroundColour(label_color);
#endif
    manifold_warning_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle(m_warning_icon_name));
    auto *sizer_manifold = new wxBoxSizer(wxHORIZONTAL);
    sizer_manifold->Add(manifold_warning_icon, 0, wxLEFT, GetIconMargin());
    sizer_manifold->Add(info_manifold, 0, wxLEFT, GetIconMargin());
    Add(sizer_manifold, 0, wxEXPAND | wxTOP, em / 2);

    sla_hidden_items = {
        label_volume,
        info_volume,
    };

    // Start hidden
    this->Show(false);
}

void ObjectInfo::show_sizer(bool show)
{
    Show(show);
    if (show)
        manifold_warning_icon->Show(showing_manifold_warning_icon && show);
}

void ObjectInfo::update_warning_icon(const std::string &warning_icon_name)
{
    if ((showing_manifold_warning_icon = !warning_icon_name.empty()))
    {
        m_warning_icon_name = warning_icon_name;
        manifold_warning_icon->SetBitmap(*get_bmp_bundle(m_warning_icon_name));
    }
}

void ObjectInfo::sys_color_changed()
{
#ifdef _WIN32
    // Update the static box background and border colors
    if (FlatStaticBox *box = dynamic_cast<FlatStaticBox *>(GetStaticBox()))
        box->SysColorsChanged();

    // Update all label colors - use unified color accessor
    wxColour label_color = SidebarColors::Foreground();

    if (info_size)
        info_size->SetForegroundColour(label_color);
    if (info_volume)
        info_volume->SetForegroundColour(label_color);
    if (info_facets)
        info_facets->SetForegroundColour(label_color);
    if (info_manifold)
        info_manifold->SetForegroundColour(label_color);
    if (label_volume)
        label_volume->SetForegroundColour(label_color);

    // Update all child wxStaticText controls (including "Size:", "Volume:", "Facets:" labels)
    wxWindow *parent = GetStaticBox()->GetParent();
    std::function<void(wxWindow *)> update_static_text_children = [&](wxWindow *window)
    {
        if (!window)
            return;
        if (wxStaticText *text = dynamic_cast<wxStaticText *>(window))
            text->SetForegroundColour(label_color);
        for (wxWindow *child : window->GetChildren())
            update_static_text_children(child);
    };
    update_static_text_children(GetStaticBox());
#endif
}

// ============================================================================
// TabbedSettingsPanel Implementation - Base class for fixed-header settings panels
// ============================================================================

TabbedSettingsPanel::TabbedSettingsPanel(wxWindow *parent, Plater *plater)
    : wxPanel(parent, wxID_ANY), m_plater(plater), m_active_tab_index(0)
{
    // NOTE: Do NOT call BuildUI() here - it calls virtual GetTabDefinitions()
    // Derived classes must call BuildUI() in their constructors
}

void TabbedSettingsPanel::BuildUI()
{
    // Set background color using unified accessor
    SetBackgroundColour(SidebarColors::Background());

    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // Get tab definitions from subclass
    auto definitions = GetTabDefinitions();
    m_tabs.clear();
    m_tabs.reserve(definitions.size());

    // Track the first visible tab for initial expansion
    int first_visible_tab = -1;

    // Create a CollapsibleSection for each tab (accordion style)
    for (size_t i = 0; i < definitions.size(); ++i)
    {
        const auto &def = definitions[i];

        // Check if this tab should be visible based on sidebar visibility settings
        bool tab_visible = IsTabVisible(static_cast<int>(i));

        // Create collapsible section - first visible one expanded by default
        bool initially_expanded = tab_visible && (first_visible_tab == -1 || static_cast<int>(i) == m_active_tab_index);
        if (tab_visible && first_visible_tab == -1)
            first_visible_tab = static_cast<int>(i);

        auto *section = new CollapsibleSection(this, def.title, initially_expanded && tab_visible);

        // Set icon if available
        if (!def.icon_name.IsEmpty())
            section->SetHeaderIcon(*get_bmp_bundle(def.icon_name.ToStdString()));

        // Add orange bullet to distinguish sub-tabs from main sections
        section->SetBulletColor(wxColour(0xEA, 0xA0, 0x32));

        // Create scrolled content area for this tab using custom ScrollablePanel
        auto *scroll = new ScrollablePanel(section, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        scroll->sys_color_changed();

        // Create sizer for scroll area now (content added later in EnsureContentBuilt)
        auto *scroll_sizer = new wxBoxSizer(wxVERTICAL);
        scroll->SetSizer(scroll_sizer);

        // Store tab state
        TabState state;
        state.definition = def;
        state.section = section;
        state.scroll_area = scroll;
        state.content = nullptr;
        state.content_built = false;
        m_tabs.push_back(std::move(state));

        // Wire up accordion behavior - when this section expands, collapse others
        int tab_index = static_cast<int>(i);
        section->SetOnExpandChanged(
            [this, tab_index](bool expanded)
            {
                if (expanded)
                {
                    // Freeze to prevent layout thrashing during accordion behavior
                    Freeze();

                    // Collapse all other sections
                    for (size_t j = 0; j < m_tabs.size(); ++j)
                    {
                        if (static_cast<int>(j) != tab_index && m_tabs[j].section)
                            m_tabs[j].section->SetExpanded(false);
                    }
                    // Build content if needed and switch
                    int old_index = m_active_tab_index;
                    m_active_tab_index = tab_index;
                    EnsureContentBuilt(tab_index);
                    OnTabSwitched(old_index, tab_index);

                    Thaw();
                }
                // Update sizer proportions - expanded section gets proportion 1, others get 0
                UpdateSizerProportions();
            });

        section->SetContent(scroll);

        // Hide the section if tab is not visible (all settings hidden)
        if (!tab_visible)
            section->Hide();

        // Add with proportion 1 if initially expanded, 0 if collapsed
        int proportion = (initially_expanded && tab_visible) ? 1 : 0;
        m_main_sizer->Add(section, proportion, wxEXPAND);
    }

    SetSizer(m_main_sizer);

    // Update active tab index to first visible tab if current is hidden
    if (first_visible_tab >= 0 && !IsTabVisible(m_active_tab_index))
        m_active_tab_index = first_visible_tab;

    // Build content for all tabs eagerly so m_setting_controls is fully populated
    // (required for two-way sync between main settings tabs and sidebar)
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
        EnsureContentBuilt(i);
}

void TabbedSettingsPanel::SwitchToTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;

    if (index == m_active_tab_index && m_tabs[index].section && m_tabs[index].section->IsExpanded())
        return; // Already on this tab and expanded

    // Expand the target section - the OnExpandChanged callback handles:
    // - Collapsing other sections
    // - Building content if needed
    // - Updating m_active_tab_index
    if (m_tabs[index].section)
    {
        m_tabs[index].section->SetExpanded(true);
    }

    Layout();
}

void TabbedSettingsPanel::SwitchToTabByName(const wxString &name)
{
    for (size_t i = 0; i < m_tabs.size(); ++i)
    {
        if (m_tabs[i].definition.name == name)
        {
            SwitchToTab(static_cast<int>(i));
            return;
        }
    }
}

wxString TabbedSettingsPanel::GetActiveTabName() const
{
    if (m_active_tab_index >= 0 && m_active_tab_index < static_cast<int>(m_tabs.size()))
        return m_tabs[m_active_tab_index].definition.name;
    return wxEmptyString;
}

wxPanel *TabbedSettingsPanel::GetContentArea() const
{
    if (m_active_tab_index >= 0 && m_active_tab_index < static_cast<int>(m_tabs.size()))
        return m_tabs[m_active_tab_index].scroll_area ? m_tabs[m_active_tab_index].scroll_area->GetContentPanel()
                                                      : nullptr;
    return nullptr;
}

wxPanel *TabbedSettingsPanel::GetContentArea(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_tabs.size()))
        return m_tabs[index].scroll_area ? m_tabs[index].scroll_area->GetContentPanel() : nullptr;
    return nullptr;
}

void TabbedSettingsPanel::EnsureContentBuilt(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;

    if (m_tabs[index].content_built)
        return;

    // Freeze to prevent layout thrashing during control creation
    m_tabs[index].scroll_area->Freeze();

    // Temporarily set active tab index so GetContentArea() (no-arg) returns the
    // correct scroll area for this tab. BuildXxxContent() methods use GetContentArea()
    // to parent their controls, so this must match the tab being built.
    int saved_active_tab = m_active_tab_index;
    m_active_tab_index = index;

    // Ask subclass to build the content
    wxPanel *content = BuildTabContent(index);

    // Restore active tab index
    m_active_tab_index = saved_active_tab;
    if (content)
    {
        m_tabs[index].content = content;

        // Add content to this tab's scroll area (sizer already exists from BuildUI)
        auto *scroll_sizer = m_tabs[index].scroll_area->GetSizer();
        if (scroll_sizer)
        {
            scroll_sizer->Add(content, 1, wxEXPAND);
            m_tabs[index].scroll_area->FitInside();
            m_tabs[index].scroll_area->Layout();
        }
    }
    m_tabs[index].content_built = true;

    m_tabs[index].scroll_area->Thaw();

    // Apply toggle logic to set initial enable/disable state of dependent options
    // This must be called after content is built so all controls exist
    ApplyToggleLogic();

    // Bind dead space click handlers on new content to commit field changes
    // Use CallAfter to defer until after Plater construction is complete
    // (during construction, m_plater->sidebar() would crash because Plater::p is not yet assigned)
    if (content && m_plater)
    {
        wxWindow *content_ptr = content;
        CallAfter(
            [this, content_ptr]()
            {
                if (m_plater)
                {
                    m_plater->sidebar().BindDeadSpaceHandlers(content_ptr);
                }
            });
    }
}

void TabbedSettingsPanel::UpdateContentLayout()
{
    // Update layout of active tab's scroll area
    if (m_active_tab_index >= 0 && m_active_tab_index < static_cast<int>(m_tabs.size()))
    {
        if (m_tabs[m_active_tab_index].scroll_area)
        {
            m_tabs[m_active_tab_index].scroll_area->Layout();
            m_tabs[m_active_tab_index].scroll_area->FitInside();
        }
    }
    Layout();
}

void TabbedSettingsPanel::UpdateSizerProportions()
{
    if (!m_main_sizer)
        return;

    // Give proportion 1 to expanded section, 0 to collapsed
    for (size_t i = 0; i < m_tabs.size(); ++i)
    {
        wxSizerItem *item = m_main_sizer->GetItem(m_tabs[i].section);
        if (item)
        {
            bool is_expanded = m_tabs[i].section && m_tabs[i].section->IsExpanded();
            item->SetProportion(is_expanded ? 1 : 0);
        }
    }

    m_main_sizer->Layout();
    Layout();

    // Propagate layout to parent
    if (GetParent())
        GetParent()->Layout();
}

void TabbedSettingsPanel::RebuildContent()
{
    // Release any mouse capture before destroying windows
    // This prevents crashes in NotifyCaptureLost when windows are destroyed
    // while still having mouse capture
    wxWindow *captured = wxWindow::GetCapture();
    if (captured)
    {
        // Check if the captured window is a descendant of this panel
        wxWindow *parent = captured->GetParent();
        while (parent)
        {
            if (parent == this)
            {
                captured->ReleaseMouse();
                break;
            }
            parent = parent->GetParent();
        }
    }

    // Clear setting controls map BEFORE destroying windows
    // This prevents stale pointers from being accessed in ApplyToggleLogic()
    ClearSettingControls();

    // Destroy all existing sections
    for (auto &tab : m_tabs)
    {
        if (tab.section)
        {
            tab.section->Destroy();
            tab.section = nullptr;
        }
        tab.scroll_area = nullptr;
        tab.content = nullptr;
        tab.content_built = false;
    }
    m_tabs.clear();

    // Clear our sizer
    GetSizer()->Clear(false);

    // Rebuild everything
    BuildUI();

    Layout();
}

void TabbedSettingsPanel::ApplyDarkModeToPanel(wxWindow *window)
{
    ApplyDarkModeToStaticBoxes(window);
}

void TabbedSettingsPanel::ToggleOptionControl(wxWindow *control, bool enable)
{
    if (!control)
        return;

    // Handle our custom TextInput widget - it has its own Enable() that handles theming
    if (TextInput *text_input = dynamic_cast<TextInput *>(control))
    {
        text_input->Enable(enable);
    }
    // Handle our custom SpinInput widget
    else if (SpinInputBase *spin = dynamic_cast<SpinInputBase *>(control))
    {
        spin->Enable(enable);
    }
    // Handle our custom ComboBox widget
    else if (::ComboBox *combo = dynamic_cast<::ComboBox *>(control))
    {
        combo->Enable(enable);
    }
    // Handle our custom CheckBox widget
    else if (::CheckBox *checkbox = dynamic_cast<::CheckBox *>(control))
    {
        checkbox->Enable(enable);
    }
    // For plain wxTextCtrl on Windows: use SetEditable instead of Enable
    else if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(control))
    {
#ifdef _WIN32
        // Keep control enabled but make it read-only - this allows SetBackgroundColour to work
        text->SetEditable(enable);
        wxColour bg = enable ? SidebarColors::InputBackground() : SidebarColors::DisabledBackground();
        wxColour fg = enable ? SidebarColors::InputForeground() : SidebarColors::DisabledForeground();
        text->SetBackgroundColour(bg);
        text->SetForegroundColour(fg);
        text->Refresh();
#else
        text->Enable(enable);
#endif
    }
    else
    {
        // For other controls, use normal Enable
        control->Enable(enable);
    }
}

TabbedSettingsPanel::RowUIContext TabbedSettingsPanel::CreateRowUIBase(wxWindow *parent, const std::string &opt_key,
                                                                       const wxString &label)
{
    RowUIContext ctx;
    int em = wxGetApp().em_unit();

    // Get the option definition
    ctx.opt_def = print_config_def.get(opt_key);
    if (!ctx.opt_def)
        return ctx; // Return empty context

    // Get tooltip
    ctx.tooltip = ctx.opt_def->tooltip.empty() ? wxString() : from_u8(ctx.opt_def->tooltip);

    // Create row sizer
    ctx.row_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Left side sizer: icons + label (proportion 1 = 50% of row)
    ctx.left_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    // Create lock icon
    ctx.lock_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("lock_closed"));
    ctx.lock_icon->SetMinSize(GetScaledIconSizeWx());
    ctx.lock_icon->SetBackgroundColour(bg_color);
    ctx.lock_icon->SetToolTip(_L("Value is same as in the system preset"));
    ctx.left_sizer->Add(ctx.lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    // Create undo icon
    ctx.undo_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("dot"));
    ctx.undo_icon->SetMinSize(GetScaledIconSizeWx());
    ctx.undo_icon->SetBackgroundColour(bg_color);
    ctx.left_sizer->Add(ctx.undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    // Label with colon - use ellipsis to allow shrinking
    wxString label_with_colon = label + ":";
    ctx.label_text = new wxStaticText(parent, wxID_ANY, label_with_colon, wxDefaultPosition, wxDefaultSize,
                                      wxST_ELLIPSIZE_END);
    ctx.label_text->SetMinSize(wxSize(1, -1)); // Allow label to shrink
    ctx.label_text->SetBackgroundColour(bg_color);
    if (!ctx.tooltip.empty())
        ctx.label_text->SetToolTip(ctx.tooltip);
    ctx.left_sizer->Add(ctx.label_text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

    // Add left side to row (50% of width)
    ctx.row_sizer->Add(ctx.left_sizer, 1, wxEXPAND);

    return ctx;
}

void TabbedSettingsPanel::BindUndoHandler(wxStaticBitmap *undo_icon, const std::string &opt_key,
                                          std::function<void(const std::string &)> on_setting_changed)
{
    if (!undo_icon)
        return;

    undo_icon->Bind(wxEVT_LEFT_DOWN,
                    [this, opt_key, on_setting_changed](wxMouseEvent &)
                    {
                        // Get original value and restore it
                        const Preset *system_preset = GetSystemPresetParent();
                        if (system_preset && system_preset->config.has(opt_key))
                        {
                            DynamicPrintConfig &config = GetEditedConfig();
                            std::string original_value = system_preset->config.opt_serialize(opt_key);
                            config.set_deserialize_strict(opt_key, original_value);
                            on_setting_changed(opt_key);
                        }
                    });
}

void TabbedSettingsPanel::UpdateUndoUICommon(const std::string &opt_key, wxWindow *undo_icon, wxWindow *lock_icon,
                                             const std::string &original_value)
{
    const DynamicPrintConfig &config = GetEditedConfig();

    // Get current value from config
    std::string current_value;
    if (config.has(opt_key))
        current_value = config.opt_serialize(opt_key);

    // Check if value differs from original (for undo icon)
    bool is_modified = (current_value != original_value);

    // Update undo icon - show dot when unchanged, undo arrow when modified
    if (auto *bmp = dynamic_cast<wxStaticBitmap *>(undo_icon))
    {
        if (is_modified)
        {
            bmp->SetBitmap(*get_bmp_bundle("undo"));
            bmp->SetToolTip(_L("Click to revert to original value"));
            bmp->SetCursor(wxCursor(wxCURSOR_HAND));
        }
        else
        {
            bmp->SetBitmap(*get_bmp_bundle("dot"));
            bmp->SetToolTip(wxEmptyString);
            bmp->SetCursor(wxNullCursor);
        }
    }

    // Check if value differs from system preset (for lock icon)
    const Preset *system_preset = GetSystemPresetParent();
    bool differs_from_system = false;

    if (system_preset && system_preset->config.has(opt_key))
    {
        std::string system_value = system_preset->config.opt_serialize(opt_key);
        differs_from_system = (current_value != system_value);
    }

    // Update lock icon - show lock_open when different from system, lock_closed when same
    if (auto *bmp = dynamic_cast<wxStaticBitmap *>(lock_icon))
    {
        if (differs_from_system)
        {
            bmp->SetBitmap(*get_bmp_bundle("lock_open"));
            bmp->SetToolTip(_L("Value differs from system preset"));
        }
        else
        {
            bmp->SetBitmap(*get_bmp_bundle("lock_closed"));
            bmp->SetToolTip(_L("Value is same as in the system preset"));
        }
    }
}

void TabbedSettingsPanel::msw_rescale()
{
    // Rescale each section
    for (auto &tab : m_tabs)
    {
        if (tab.section)
            tab.section->msw_rescale();
    }
    Layout();
}

void TabbedSettingsPanel::sys_color_changed()
{
    // Update panel background using unified accessor
    SetBackgroundColour(SidebarColors::Background());

    // Update each section
    for (auto &tab : m_tabs)
    {
        if (tab.section)
        {
            tab.section->sys_color_changed();
            // Refresh header icon for new theme (icons have dark/light variants)
            if (!tab.definition.icon_name.IsEmpty())
                tab.section->SetHeaderIcon(*get_bmp_bundle(tab.definition.icon_name.ToStdString()));
        }

        if (tab.scroll_area)
        {
            // Update custom scrollable panel for theme change
            tab.scroll_area->sys_color_changed();
        }

        if (tab.content)
            ApplyDarkModeToPanel(tab.content);
    }

    // Let subclass update its content
    OnSysColorChanged();

    Refresh();
}

// ============================================================================
// PrintSettingsPanel Implementation - Print settings with tabbed categories
// ============================================================================

PrintSettingsPanel::PrintSettingsPanel(wxWindow *parent, Plater *plater) : TabbedSettingsPanel(parent, plater)
{
    BuildUI();
}

DynamicPrintConfig &PrintSettingsPanel::GetEditedConfig()
{
    return wxGetApp().preset_bundle->prints.get_edited_preset().config;
}

const DynamicPrintConfig &PrintSettingsPanel::GetEditedConfig() const
{
    return wxGetApp().preset_bundle->prints.get_edited_preset().config;
}

const Preset *PrintSettingsPanel::GetSystemPresetParent() const
{
    return wxGetApp().preset_bundle->prints.get_selected_preset_parent();
}

Tab *PrintSettingsPanel::GetSyncTab() const
{
    return wxGetApp().get_tab(Preset::TYPE_PRINT);
}

std::vector<TabbedSettingsPanel::TabDefinition> PrintSettingsPanel::GetTabDefinitions()
{
    return {{"layers", _L("Layers and perimeters"), "layers"},
            {"infill", _L("Infill"), "infill"},
            {"skirt", _L("Skirt and brim"), "skirt+brim"},
            {"support", _L("Support material"), "support"},
            {"speed", _L("Speed"), "time"},
            {"extruders", _L("Multiple Extruders"), "funnel"},
            {"advanced", _L("Advanced"), "wrench"},
            {"output", _L("Output options"), "output+page_white"}};
}

bool PrintSettingsPanel::IsTabVisible(int tab_index) const
{
    // Use category-based visibility checking from print_config_def
    // This automatically includes any new options added to these categories
    switch (tab_index)
    {
    case TAB_LAYERS:
        return is_any_category_visible({"Layers and Perimeters", "Fuzzy skin"});

    case TAB_INFILL:
        return is_any_category_visible({"Infill", "Ironing"});

    case TAB_SKIRT_BRIM:
        return is_any_category_visible({"Skirt and brim"});

    case TAB_SUPPORT:
        return is_any_category_visible({"Support material"});

    case TAB_SPEED:
        return is_any_category_visible({"Speed"});

    case TAB_EXTRUDERS:
        return is_any_category_visible({"Extruders", "Wipe options"});

    case TAB_ADVANCED:
        return is_any_category_visible({"Advanced", "Extrusion Width"});

    case TAB_OUTPUT:
        // Output options don't have a dedicated category in PrintConfig, use explicit list
        return has_any_visible_setting(
            {"complete_objects", "gcode_comments", "gcode_label_objects", "output_filename_format"});

    default:
        return true;
    }
}

wxPanel *PrintSettingsPanel::BuildTabContent(int tab_index)
{
    switch (tab_index)
    {
    case TAB_LAYERS:
        return BuildLayersContent();
    case TAB_INFILL:
        return BuildInfillContent();
    case TAB_SKIRT_BRIM:
        return BuildSkirtBrimContent();
    case TAB_SUPPORT:
        return BuildSupportContent();
    case TAB_SPEED:
        return BuildSpeedContent();
    case TAB_EXTRUDERS:
        return BuildExtrudersContent();
    case TAB_ADVANCED:
        return BuildAdvancedContent();
    case TAB_OUTPUT:
        return BuildOutputContent();
    default:
        return nullptr;
    }
}

void PrintSettingsPanel::OnSysColorChanged()
{
    // Update all setting controls - call SysColorsChanged() on each custom widget
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (!ui_elem.control)
            continue;

        // Try each custom widget type that has SysColorsChanged/sys_color_changed
        if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *checkbox = dynamic_cast<::CheckBox *>(ui_elem.control))
            checkbox->sys_color_changed();
    }
}

wxPanel *PrintSettingsPanel::BuildLayersContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Layer height group
    if (has_any_visible_setting({"layer_height", "first_layer_height"}))
    {
        auto *layer_group = CreateFlatStaticBoxSizer(content, _L("Layer height"));
        CreateSettingRow(content, layer_group, "layer_height", _L("Layer height"));
        CreateSettingRow(content, layer_group, "first_layer_height", _L("First layer height"));
        sizer->Add(layer_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Vertical shells group
    if (has_any_visible_setting({"perimeters", "spiral_vase"}))
    {
        auto *vshells_group = CreateFlatStaticBoxSizer(content, _L("Vertical shells"));
        CreateSettingRow(content, vshells_group, "perimeters", _L("Perimeters"));
        CreateSettingRow(content, vshells_group, "spiral_vase", _L("Spiral vase"));
        sizer->Add(vshells_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Horizontal shells group
    if (has_any_visible_setting(
            {"top_solid_layers", "bottom_solid_layers", "top_solid_min_thickness", "bottom_solid_min_thickness"}))
    {
        auto *hshells_group = CreateFlatStaticBoxSizer(content, _L("Horizontal shells"));
        CreateSettingRow(content, hshells_group, "top_solid_layers", _L("Top solid layers"));
        CreateSettingRow(content, hshells_group, "bottom_solid_layers", _L("Bottom solid layers"));
        CreateSettingRow(content, hshells_group, "top_solid_min_thickness", _L("Top min thickness"));
        CreateSettingRow(content, hshells_group, "bottom_solid_min_thickness", _L("Bottom min thickness"));
        sizer->Add(hshells_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Interlock Perimeters group (before Quality per Tab.cpp order)
    if (has_any_visible_setting({"interlock_perimeters_enabled", "interlock_perimeter_count",
                                 "interlock_perimeter_overlap", "interlock_flow_detection"}))
    {
        auto *interlock_group = CreateFlatStaticBoxSizer(content, _L("Interlocking"));
        CreateSettingRow(content, interlock_group, "interlock_perimeters_enabled", _L("Enable interlock perimeters"));
        CreateSettingRow(content, interlock_group, "interlock_perimeter_count", _L("Interlock perimeter count"));
        CreateSettingRow(content, interlock_group, "interlock_perimeter_overlap", _L("Interlock perimeter overlap"));
        CreateSettingRow(content, interlock_group, "interlock_flow_detection", _L("Interlock flow detection"));
        sizer->Add(interlock_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Quality group
    if (has_any_visible_setting({"extra_perimeters", "extra_perimeters_on_overhangs", "ensure_vertical_shell_thickness",
                                 "avoid_crossing_curled_overhangs", "avoid_crossing_perimeters",
                                 "avoid_crossing_perimeters_max_detour", "overhangs"}))
    {
        auto *quality_group = CreateFlatStaticBoxSizer(content, _L("Quality"));
        CreateSettingRow(content, quality_group, "extra_perimeters", _L("Extra perimeters if needed"));
        CreateSettingRow(content, quality_group, "extra_perimeters_on_overhangs", _L("Extra perimeters on overhangs"));
        CreateSettingRow(content, quality_group, "ensure_vertical_shell_thickness",
                         _L("Ensure vertical shell thickness"));
        CreateSettingRow(content, quality_group, "avoid_crossing_curled_overhangs",
                         _L("Avoid crossing curled overhangs"));
        CreateSettingRow(content, quality_group, "avoid_crossing_perimeters", _L("Avoid crossing perimeters"));
        CreateSettingRow(content, quality_group, "avoid_crossing_perimeters_max_detour", _L("Max detour length"));
        CreateSettingRow(content, quality_group, "overhangs", _L("Detect bridging perimeters"));
        sizer->Add(quality_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Advanced group (includes scarf seam per Tab.cpp)
    if (has_any_visible_setting({"perimeter_generator", "seam_position", "seam_gap_distance", "staggered_inner_seams",
                                 "external_perimeters_first", "scarf_seam_placement", "scarf_seam_only_on_smooth",
                                 "scarf_seam_start_height", "scarf_seam_entire_loop", "scarf_seam_length",
                                 "scarf_seam_max_segment_length", "scarf_seam_on_inner_perimeters"}))
    {
        auto *adv_group = CreateFlatStaticBoxSizer(content, _L("Advanced"));
        CreateSettingRow(content, adv_group, "perimeter_generator", _L("Perimeter generator"));
        CreateSettingRow(content, adv_group, "seam_position", _L("Seam position"));
        CreateSettingRow(content, adv_group, "seam_gap_distance", _L("Seam gap"));
        CreateSettingRow(content, adv_group, "staggered_inner_seams", _L("Staggered inner seams"));
        CreateSettingRow(content, adv_group, "external_perimeters_first", _L("External perimeters first"));
        CreateSettingRow(content, adv_group, "scarf_seam_placement", _L("Scarf seam placement"));
        CreateSettingRow(content, adv_group, "scarf_seam_only_on_smooth", _L("Only on smooth perimeters"));
        CreateSettingRow(content, adv_group, "scarf_seam_start_height", _L("Scarf start height"));
        CreateSettingRow(content, adv_group, "scarf_seam_entire_loop", _L("Scarf entire loop"));
        CreateSettingRow(content, adv_group, "scarf_seam_length", _L("Scarf length"));
        CreateSettingRow(content, adv_group, "scarf_seam_max_segment_length", _L("Scarf max segment length"));
        CreateSettingRow(content, adv_group, "scarf_seam_on_inner_perimeters", _L("Scarf on inner perimeters"));
        sizer->Add(adv_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Top surface flow group
    if (has_any_visible_setting({"top_surface_flow_reduction", "top_surface_visibility_detection"}))
    {
        auto *top_surface_group = CreateFlatStaticBoxSizer(content, _L("Top surface flow"));
        CreateSettingRow(content, top_surface_group, "top_surface_flow_reduction", _L("Top surface flow reduction"));
        CreateSettingRow(content, top_surface_group, "top_surface_visibility_detection", _L("Visibility detection"));
        sizer->Add(top_surface_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Fuzzy skin group (order matches Tab.cpp)
    if (has_any_visible_setting({"fuzzy_skin_painted_perimeters", "fuzzy_skin", "fuzzy_skin_thickness",
                                 "fuzzy_skin_point_dist", "fuzzy_skin_on_top", "fuzzy_skin_first_layer",
                                 "fuzzy_skin_visibility_detection", "fuzzy_skin_noise_type", "fuzzy_skin_mode",
                                 "fuzzy_skin_point_placement", "fuzzy_skin_scale", "fuzzy_skin_octaves",
                                 "fuzzy_skin_persistence"}))
    {
        auto *fuzzy_group = CreateFlatStaticBoxSizer(content, _L("Fuzzy skin"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_painted_perimeters", _L("Painted perimeters"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin", _L("Fuzzy skin type"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_thickness", _L("Fuzzy skin thickness"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_point_dist", _L("Fuzzy skin point distance"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_on_top", _L("Fuzzy skin on top"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_first_layer", _L("Fuzzy skin on first layer"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_visibility_detection", _L("Visibility detection"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_noise_type", _L("Noise type"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_mode", _L("Fuzzy skin mode"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_point_placement", _L("Point placement"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_scale", _L("Scale"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_octaves", _L("Octaves"));
        CreateSettingRow(content, fuzzy_group, "fuzzy_skin_persistence", _L("Persistence"));
        sizer->Add(fuzzy_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Only one perimeter group
    if (has_any_visible_setting({"top_one_perimeter_type", "only_one_perimeter_first_layer"}))
    {
        auto *one_perim_group = CreateFlatStaticBoxSizer(content, _L("Single perimeter"));
        CreateSettingRow(content, one_perim_group, "top_one_perimeter_type", _L("Top one perimeter type"));
        CreateSettingRow(content, one_perim_group, "only_one_perimeter_first_layer",
                         _L("Only one perimeter on first layer"));
        sizer->Add(one_perim_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *PrintSettingsPanel::BuildInfillContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Infill group
    if (has_any_visible_setting({"fill_density", "fill_pattern", "solid_fill_pattern", "top_fill_pattern",
                                 "bottom_fill_pattern", "infill_anchor", "infill_anchor_max"}))
    {
        auto *infill_group = CreateFlatStaticBoxSizer(content, _L("Infill"));
        CreateSettingRow(content, infill_group, "fill_density", _L("Fill density"));
        CreateSettingRow(content, infill_group, "fill_pattern", _L("Fill pattern"));
        CreateSettingRow(content, infill_group, "solid_fill_pattern", _L("Solid fill pattern"));
        CreateSettingRow(content, infill_group, "top_fill_pattern", _L("Top fill pattern"));
        CreateSettingRow(content, infill_group, "bottom_fill_pattern", _L("Bottom fill pattern"));
        CreateSettingRow(content, infill_group, "infill_anchor", _L("Infill anchor length"));
        CreateSettingRow(content, infill_group, "infill_anchor_max", _L("Infill anchor max length"));
        sizer->Add(infill_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Ironing group
    if (has_any_visible_setting({"ironing", "ironing_type", "ironing_flowrate", "ironing_spacing"}))
    {
        auto *ironing_group = CreateFlatStaticBoxSizer(content, _L("Ironing"));
        CreateSettingRow(content, ironing_group, "ironing", _L("Enable ironing"));
        CreateSettingRow(content, ironing_group, "ironing_type", _L("Ironing type"));
        CreateSettingRow(content, ironing_group, "ironing_flowrate", _L("Flow rate"));
        CreateSettingRow(content, ironing_group, "ironing_spacing", _L("Spacing"));
        sizer->Add(ironing_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Reducing printing time group
    if (has_any_visible_setting({"automatic_infill_combination", "automatic_infill_combination_max_layer_height",
                                 "infill_every_layers", "narrow_solid_infill_concentric",
                                 "narrow_solid_infill_threshold"}))
    {
        auto *time_group = CreateFlatStaticBoxSizer(content, _L("Time savings"));
        CreateSettingRow(content, time_group, "automatic_infill_combination", _L("Automatic infill combination"));
        CreateSettingRow(content, time_group, "automatic_infill_combination_max_layer_height",
                         _L("Max combined layer height"));
        CreateSettingRow(content, time_group, "infill_every_layers", _L("Combine infill every"));
        CreateSettingRow(content, time_group, "narrow_solid_infill_concentric", _L("Narrow solid infill concentric"));
        CreateSettingRow(content, time_group, "narrow_solid_infill_threshold", _L("Narrow solid infill threshold"));
        sizer->Add(time_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Advanced group
    if (has_any_visible_setting({"solid_infill_every_layers", "fill_angle", "solid_infill_below_area", "bridge_angle",
                                 "merge_top_solid_infills", "only_retract_when_crossing_perimeters", "infill_first"}))
    {
        auto *adv_group = CreateFlatStaticBoxSizer(content, _L("Advanced"));
        CreateSettingRow(content, adv_group, "solid_infill_every_layers", _L("Solid infill every"));
        CreateSettingRow(content, adv_group, "fill_angle", _L("Fill angle"));
        CreateSettingRow(content, adv_group, "solid_infill_below_area", _L("Solid infill threshold area"));
        CreateSettingRow(content, adv_group, "bridge_angle", _L("Bridge angle"));
        CreateSettingRow(content, adv_group, "merge_top_solid_infills", _L("Merge top solid infills"));
        CreateSettingRow(content, adv_group, "only_retract_when_crossing_perimeters",
                         _L("Only retract when crossing perimeters"));
        CreateSettingRow(content, adv_group, "infill_first", _L("Infill before perimeters"));
        sizer->Add(adv_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *PrintSettingsPanel::BuildSkirtBrimContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Skirt group
    if (has_any_visible_setting({"skirts", "skirt_distance", "skirt_height", "draft_shield", "min_skirt_length"}))
    {
        auto *skirt_group = CreateFlatStaticBoxSizer(content, _L("Skirt"));
        CreateSettingRow(content, skirt_group, "skirts", _L("Loops (minimum)"));
        CreateSettingRow(content, skirt_group, "skirt_distance", _L("Distance from object"));
        CreateSettingRow(content, skirt_group, "skirt_height", _L("Skirt height"));
        CreateSettingRow(content, skirt_group, "draft_shield", _L("Draft shield"));
        CreateSettingRow(content, skirt_group, "min_skirt_length", _L("Minimum extrusion length"));
        sizer->Add(skirt_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Brim group
    if (has_any_visible_setting(
            {"brim_type", "brim_width", "brim_separation", "brim_ears_max_angle", "brim_ears_detection_length"}))
    {
        auto *brim_group = CreateFlatStaticBoxSizer(content, _L("Brim"));
        CreateSettingRow(content, brim_group, "brim_type", _L("Brim type"));
        CreateSettingRow(content, brim_group, "brim_width", _L("Brim width"));
        CreateSettingRow(content, brim_group, "brim_separation", _L("Brim separation"));
        CreateSettingRow(content, brim_group, "brim_ears_max_angle", _L("Brim ears max angle"));
        CreateSettingRow(content, brim_group, "brim_ears_detection_length", _L("Brim ears detection length"));
        sizer->Add(brim_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *PrintSettingsPanel::BuildSupportContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Support material group
    if (has_any_visible_setting({"support_material", "support_material_auto", "support_material_style",
                                 "support_material_threshold", "support_material_enforce_layers",
                                 "raft_first_layer_density", "raft_first_layer_expansion"}))
    {
        auto *support_group = CreateFlatStaticBoxSizer(content, _L("Support material"));
        CreateSettingRow(content, support_group, "support_material", _L("Generate support material"));
        CreateSettingRow(content, support_group, "support_material_auto", _L("Auto generated supports"));
        CreateSettingRow(content, support_group, "support_material_style", _L("Style"));
        CreateSettingRow(content, support_group, "support_material_threshold", _L("Overhang threshold"));
        CreateSettingRow(content, support_group, "support_material_enforce_layers", _L("Enforce support for first"));
        CreateSettingRow(content, support_group, "raft_first_layer_density", _L("Raft first layer density"));
        CreateSettingRow(content, support_group, "raft_first_layer_expansion", _L("Raft first layer expansion"));
        sizer->Add(support_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Raft group
    if (has_any_visible_setting({"raft_layers", "raft_contact_distance", "raft_expansion"}))
    {
        auto *raft_group = CreateFlatStaticBoxSizer(content, _L("Raft"));
        CreateSettingRow(content, raft_group, "raft_layers", _L("Raft layers"));
        CreateSettingRow(content, raft_group, "raft_contact_distance", _L("Raft contact Z distance"));
        CreateSettingRow(content, raft_group, "raft_expansion", _L("Raft expansion"));
        sizer->Add(raft_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Options for support material and raft group
    if (has_any_visible_setting({"support_material_contact_distance",
                                 "support_material_contact_distance_custom",
                                 "support_material_top_contact_extrusion_width",
                                 "support_material_bottom_contact_distance",
                                 "support_material_bottom_contact_extrusion_width",
                                 "support_material_pattern",
                                 "support_material_bridge_no_gap",
                                 "support_material_with_sheath",
                                 "support_material_spacing",
                                 "support_material_angle",
                                 "support_material_closing_radius",
                                 "support_material_min_area",
                                 "support_material_interface_layers",
                                 "support_material_bottom_interface_layers",
                                 "support_material_interface_pattern",
                                 "support_material_interface_spacing",
                                 "support_material_interface_contact_loops",
                                 "support_material_buildplate_only",
                                 "support_material_xy_spacing",
                                 "dont_support_bridges"}))
    {
        auto *opts_group = CreateFlatStaticBoxSizer(content, _L("Options for support material and raft"));
        CreateSettingRow(content, opts_group, "support_material_contact_distance", _L("Contact Z distance"));
        CreateSettingRow(content, opts_group, "support_material_contact_distance_custom",
                         _L("Custom contact Z distance"));
        CreateSettingRow(content, opts_group, "support_material_top_contact_extrusion_width",
                         _L("Top contact extrusion width"));
        CreateSettingRow(content, opts_group, "support_material_bottom_contact_distance",
                         _L("Bottom contact Z distance"));
        CreateSettingRow(content, opts_group, "support_material_bottom_contact_extrusion_width",
                         _L("Bottom contact extrusion width"));
        CreateSettingRow(content, opts_group, "support_material_pattern", _L("Pattern"));
        CreateSettingRow(content, opts_group, "support_material_bridge_no_gap", _L("Bridge with no gap"));
        CreateSettingRow(content, opts_group, "support_material_with_sheath", _L("With sheath around support"));
        CreateSettingRow(content, opts_group, "support_material_spacing", _L("Pattern spacing"));
        CreateSettingRow(content, opts_group, "support_material_angle", _L("Pattern angle"));
        CreateSettingRow(content, opts_group, "support_material_closing_radius", _L("Closing radius"));
        CreateSettingRow(content, opts_group, "support_material_min_area", _L("Minimum support area"));
        CreateSettingRow(content, opts_group, "support_material_interface_layers", _L("Interface layers"));
        CreateSettingRow(content, opts_group, "support_material_bottom_interface_layers",
                         _L("Bottom interface layers"));
        CreateSettingRow(content, opts_group, "support_material_interface_pattern", _L("Interface pattern"));
        CreateSettingRow(content, opts_group, "support_material_interface_spacing", _L("Interface pattern spacing"));
        CreateSettingRow(content, opts_group, "support_material_interface_contact_loops",
                         _L("Interface contact loops"));
        CreateSettingRow(content, opts_group, "support_material_buildplate_only", _L("Support on build plate only"));
        CreateSettingRow(content, opts_group, "support_material_xy_spacing", _L("XY separation"));
        CreateSettingRow(content, opts_group, "dont_support_bridges", _L("Don't support bridges"));
        sizer->Add(opts_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Organic supports group
    if (has_any_visible_setting({"support_tree_angle", "support_tree_angle_slow", "support_tree_branch_diameter",
                                 "support_tree_branch_diameter_angle", "support_tree_branch_diameter_double_wall",
                                 "support_tree_tip_diameter", "support_tree_branch_distance", "support_tree_top_rate"}))
    {
        auto *organic_group = CreateFlatStaticBoxSizer(content, _L("Organic supports"));
        CreateSettingRow(content, organic_group, "support_tree_angle", _L("Branch angle"));
        CreateSettingRow(content, organic_group, "support_tree_angle_slow", _L("Branch angle slow"));
        CreateSettingRow(content, organic_group, "support_tree_branch_diameter", _L("Branch diameter"));
        CreateSettingRow(content, organic_group, "support_tree_branch_diameter_angle", _L("Branch diameter angle"));
        CreateSettingRow(content, organic_group, "support_tree_branch_diameter_double_wall",
                         _L("Branch diameter double wall"));
        CreateSettingRow(content, organic_group, "support_tree_tip_diameter", _L("Tip diameter"));
        CreateSettingRow(content, organic_group, "support_tree_branch_distance", _L("Branch distance"));
        CreateSettingRow(content, organic_group, "support_tree_top_rate", _L("Top rate"));
        sizer->Add(organic_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *PrintSettingsPanel::BuildSpeedContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Print speeds group
    if (has_any_visible_setting({"perimeter_speed", "small_perimeter_speed", "external_perimeter_speed", "infill_speed",
                                 "solid_infill_speed", "top_solid_infill_speed", "support_material_speed",
                                 "support_material_interface_speed", "bridge_speed", "over_bridge_speed",
                                 "gap_fill_speed", "ironing_speed"}))
    {
        auto *speed_group = CreateFlatStaticBoxSizer(content, _L("Print speed"));
        CreateSettingRow(content, speed_group, "perimeter_speed", _L("Perimeters"));
        CreateSettingRow(content, speed_group, "small_perimeter_speed", _L("Small perimeters"));
        CreateSettingRow(content, speed_group, "external_perimeter_speed", _L("External perimeters"));
        CreateSettingRow(content, speed_group, "infill_speed", _L("Infill"));
        CreateSettingRow(content, speed_group, "solid_infill_speed", _L("Solid infill"));
        CreateSettingRow(content, speed_group, "top_solid_infill_speed", _L("Top solid infill"));
        CreateSettingRow(content, speed_group, "support_material_speed", _L("Support material"));
        CreateSettingRow(content, speed_group, "support_material_interface_speed", _L("Support material interface"));
        CreateSettingRow(content, speed_group, "bridge_speed", _L("Bridges"));
        CreateSettingRow(content, speed_group, "over_bridge_speed", _L("Over bridge speed"));
        CreateSettingRow(content, speed_group, "gap_fill_speed", _L("Gap fill"));
        CreateSettingRow(content, speed_group, "ironing_speed", _L("Ironing"));
        sizer->Add(speed_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Dynamic overhang speed group
    if (has_any_visible_setting({"enable_dynamic_overhang_speeds", "overhang_speed_0", "overhang_speed_1",
                                 "overhang_speed_2", "overhang_speed_3"}))
    {
        auto *overhang_group = CreateFlatStaticBoxSizer(content, _L("Overhang speed"));
        CreateSettingRow(content, overhang_group, "enable_dynamic_overhang_speeds",
                         _L("Enable dynamic overhang speeds"));
        CreateSettingRow(content, overhang_group, "overhang_speed_0", _L("Overhang speed 0%"));
        CreateSettingRow(content, overhang_group, "overhang_speed_1", _L("Overhang speed 25%"));
        CreateSettingRow(content, overhang_group, "overhang_speed_2", _L("Overhang speed 50%"));
        CreateSettingRow(content, overhang_group, "overhang_speed_3", _L("Overhang speed 75%"));
        sizer->Add(overhang_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Travel speeds group
    if (has_any_visible_setting({"travel_speed", "travel_speed_z"}))
    {
        auto *travel_group = CreateFlatStaticBoxSizer(content, _L("Travel speed"));
        CreateSettingRow(content, travel_group, "travel_speed", _L("Travel"));
        CreateSettingRow(content, travel_group, "travel_speed_z", _L("Z travel"));
        sizer->Add(travel_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Modifiers group
    if (has_any_visible_setting({"first_layer_speed", "first_layer_infill_speed", "first_layer_travel_speed",
                                 "first_layer_speed_over_raft"}))
    {
        auto *mod_group = CreateFlatStaticBoxSizer(content, _L("Modifiers"));
        CreateSettingRow(content, mod_group, "first_layer_speed", _L("First layer speed"));
        CreateSettingRow(content, mod_group, "first_layer_infill_speed", _L("First layer infill speed"));
        CreateSettingRow(content, mod_group, "first_layer_travel_speed", _L("First layer travel speed"));
        CreateSettingRow(content, mod_group, "first_layer_speed_over_raft", _L("First layer speed over raft"));
        sizer->Add(mod_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Acceleration control group
    if (has_any_visible_setting({"external_perimeter_acceleration", "perimeter_acceleration",
                                 "top_solid_infill_acceleration", "solid_infill_acceleration", "infill_acceleration",
                                 "bridge_acceleration", "first_layer_acceleration",
                                 "first_layer_acceleration_over_raft", "wipe_tower_acceleration", "travel_acceleration",
                                 "travel_short_distance_acceleration", "default_acceleration"}))
    {
        auto *accel_group = CreateFlatStaticBoxSizer(content, _L("Acceleration"));
        CreateSettingRow(content, accel_group, "external_perimeter_acceleration", _L("External perimeters"));
        CreateSettingRow(content, accel_group, "perimeter_acceleration", _L("Perimeters"));
        CreateSettingRow(content, accel_group, "top_solid_infill_acceleration", _L("Top solid infill"));
        CreateSettingRow(content, accel_group, "solid_infill_acceleration", _L("Solid infill"));
        CreateSettingRow(content, accel_group, "infill_acceleration", _L("Infill"));
        CreateSettingRow(content, accel_group, "bridge_acceleration", _L("Bridges"));
        CreateSettingRow(content, accel_group, "first_layer_acceleration", _L("First layer"));
        CreateSettingRow(content, accel_group, "first_layer_acceleration_over_raft", _L("First layer over raft"));
        CreateSettingRow(content, accel_group, "wipe_tower_acceleration", _L("Wipe tower"));
        CreateSettingRow(content, accel_group, "travel_acceleration", _L("Travel"));
        CreateSettingRow(content, accel_group, "travel_short_distance_acceleration", _L("Short distance travel"));
        CreateSettingRow(content, accel_group, "default_acceleration", _L("Default"));
        sizer->Add(accel_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Autospeed group
    if (has_any_visible_setting({"max_print_speed", "max_volumetric_speed"}))
    {
        auto *auto_group = CreateFlatStaticBoxSizer(content, _L("Autospeed"));
        CreateSettingRow(content, auto_group, "max_print_speed", _L("Max print speed"));
        CreateSettingRow(content, auto_group, "max_volumetric_speed", _L("Max volumetric speed"));
        sizer->Add(auto_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Pressure equalizer group
    if (has_any_visible_setting(
            {"max_volumetric_extrusion_rate_slope_positive", "max_volumetric_extrusion_rate_slope_negative"}))
    {
        auto *pressure_group = CreateFlatStaticBoxSizer(content, _L("Pressure equalizer"));
        CreateSettingRow(content, pressure_group, "max_volumetric_extrusion_rate_slope_positive",
                         _L("Max slope positive"));
        CreateSettingRow(content, pressure_group, "max_volumetric_extrusion_rate_slope_negative",
                         _L("Max slope negative"));
        sizer->Add(pressure_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *PrintSettingsPanel::BuildExtrudersContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Extruders group
    if (has_any_visible_setting({"perimeter_extruder", "interlocking_perimeter_extruder", "infill_extruder",
                                 "solid_infill_extruder", "support_material_extruder",
                                 "support_material_interface_extruder", "wipe_tower_extruder",
                                 "bed_temperature_extruder"}))
    {
        auto *ext_group = CreateFlatStaticBoxSizer(content, _L("Extruders"));
        CreateSettingRow(content, ext_group, "perimeter_extruder", _L("Perimeter extruder"));
        CreateSettingRow(content, ext_group, "interlocking_perimeter_extruder", _L("Interlocking perimeter extruder"));
        CreateSettingRow(content, ext_group, "infill_extruder", _L("Infill extruder"));
        CreateSettingRow(content, ext_group, "solid_infill_extruder", _L("Solid infill extruder"));
        CreateSettingRow(content, ext_group, "support_material_extruder", _L("Support material extruder"));
        CreateSettingRow(content, ext_group, "support_material_interface_extruder",
                         _L("Support material interface extruder"));
        CreateSettingRow(content, ext_group, "wipe_tower_extruder", _L("Wipe tower extruder"));
        CreateSettingRow(content, ext_group, "bed_temperature_extruder", _L("Bed temperature extruder"));
        sizer->Add(ext_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Ooze prevention group
    if (has_any_visible_setting({"ooze_prevention", "standby_temperature_delta"}))
    {
        auto *ooze_group = CreateFlatStaticBoxSizer(content, _L("Ooze prevention"));
        CreateSettingRow(content, ooze_group, "ooze_prevention", _L("Enable"));
        CreateSettingRow(content, ooze_group, "standby_temperature_delta", _L("Temperature variation"));
        sizer->Add(ooze_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Wipe tower group
    if (has_any_visible_setting({"wipe_tower", "wipe_tower_width", "wipe_tower_brim_width", "wipe_tower_bridging",
                                 "wipe_tower_cone_angle", "wipe_tower_extra_spacing", "wipe_tower_extra_flow",
                                 "wipe_tower_no_sparse_layers", "single_extruder_multi_material_priming"}))
    {
        auto *wipe_group = CreateFlatStaticBoxSizer(content, _L("Wipe tower"));
        CreateSettingRow(content, wipe_group, "wipe_tower", _L("Enable"));
        CreateSettingRow(content, wipe_group, "wipe_tower_width", _L("Width"));
        CreateSettingRow(content, wipe_group, "wipe_tower_brim_width", _L("Brim width"));
        CreateSettingRow(content, wipe_group, "wipe_tower_bridging", _L("Bridging"));
        CreateSettingRow(content, wipe_group, "wipe_tower_cone_angle", _L("Cone angle"));
        CreateSettingRow(content, wipe_group, "wipe_tower_extra_spacing", _L("Extra spacing"));
        CreateSettingRow(content, wipe_group, "wipe_tower_extra_flow", _L("Extra flow"));
        CreateSettingRow(content, wipe_group, "wipe_tower_no_sparse_layers", _L("No sparse layers"));
        CreateSettingRow(content, wipe_group, "single_extruder_multi_material_priming",
                         _L("Single extruder MM priming"));
        sizer->Add(wipe_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Advanced group
    if (has_any_visible_setting({"interface_shells", "mmu_segmented_region_max_width",
                                 "mmu_segmented_region_interlocking_depth", "interlocking_beam",
                                 "interlocking_beam_width", "interlocking_orientation", "interlocking_beam_layer_count",
                                 "interlocking_depth", "interlocking_boundary_avoidance"}))
    {
        auto *mm_group = CreateFlatStaticBoxSizer(content, _L("Advanced"));
        CreateSettingRow(content, mm_group, "interface_shells", _L("Interface shells"));
        CreateSettingRow(content, mm_group, "mmu_segmented_region_max_width", _L("MMU segmented region max width"));
        CreateSettingRow(content, mm_group, "mmu_segmented_region_interlocking_depth",
                         _L("MMU segmented interlocking depth"));
        CreateSettingRow(content, mm_group, "interlocking_beam", _L("Interlocking beam"));
        CreateSettingRow(content, mm_group, "interlocking_beam_width", _L("Beam width"));
        CreateSettingRow(content, mm_group, "interlocking_orientation", _L("Orientation"));
        CreateSettingRow(content, mm_group, "interlocking_beam_layer_count", _L("Beam layer count"));
        CreateSettingRow(content, mm_group, "interlocking_depth", _L("Interlocking depth"));
        CreateSettingRow(content, mm_group, "interlocking_boundary_avoidance", _L("Boundary avoidance"));
        sizer->Add(mm_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *PrintSettingsPanel::BuildAdvancedContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Extrusion width group
    if (has_any_visible_setting({"extrusion_width", "first_layer_extrusion_width", "perimeter_extrusion_width",
                                 "external_perimeter_extrusion_width", "infill_extrusion_width",
                                 "solid_infill_extrusion_width", "bridge_extrusion_width", "top_infill_extrusion_width",
                                 "support_material_extrusion_width", "support_material_interface_extrusion_width",
                                 "automatic_extrusion_widths"}))
    {
        auto *width_group = CreateFlatStaticBoxSizer(content, _L("Extrusion width"));
        CreateSettingRow(content, width_group, "extrusion_width", _L("Default extrusion width"));

        // "Set all widths to default extrusion width" button - centered
        {
            auto *btn_row_sizer = new wxBoxSizer(wxHORIZONTAL);
            btn_row_sizer->AddStretchSpacer(1);
            auto *btn = new ScalableButton(content, wxID_ANY, "copy", _L("Set all widths to default extrusion width"),
                                           wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
            btn->SetToolTip(_L("Set all extrusion widths below to match the Default extrusion width"));
            btn->Bind(wxEVT_BUTTON,
                      [this](wxCommandEvent &)
                      {
                          DynamicPrintConfig &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
                          auto *default_width = config.option<ConfigOptionFloatOrPercent>("extrusion_width");
                          if (!default_width)
                              return;

                          static const std::vector<std::string> width_keys = {
                              "first_layer_extrusion_width",
                              "perimeter_extrusion_width",
                              "external_perimeter_extrusion_width",
                              "infill_extrusion_width",
                              "solid_infill_extrusion_width",
                              "bridge_extrusion_width",
                              "top_infill_extrusion_width",
                              "support_material_extrusion_width",
                              "support_material_interface_extrusion_width"};

                          for (const auto &key : width_keys)
                          {
                              config.set_key_value(key, default_width->clone());
                          }

                          wxGetApp().preset_bundle->prints.get_edited_preset().set_dirty(true);
                          if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINT))
                          {
                              tab->reload_config();
                              tab->update_dirty();
                              tab->update_changed_ui();
                          }
                          if (GetPlater())
                              GetPlater()->on_config_change(config);

                          RefreshFromConfig();
                      });
            btn_row_sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);
            btn_row_sizer->AddStretchSpacer(1);
            width_group->Add(btn_row_sizer, 0, wxEXPAND | wxLEFT | wxBOTTOM, em / 4);
        }

        CreateSettingRow(content, width_group, "first_layer_extrusion_width", _L("First layer"));
        CreateSettingRow(content, width_group, "perimeter_extrusion_width", _L("Perimeters"));
        CreateSettingRow(content, width_group, "external_perimeter_extrusion_width", _L("External perimeters"));
        CreateSettingRow(content, width_group, "infill_extrusion_width", _L("Infill"));
        CreateSettingRow(content, width_group, "solid_infill_extrusion_width", _L("Solid infill"));
        CreateSettingRow(content, width_group, "bridge_extrusion_width", _L("Bridge"));
        CreateSettingRow(content, width_group, "top_infill_extrusion_width", _L("Top solid infill"));
        CreateSettingRow(content, width_group, "support_material_extrusion_width", _L("Support material"));
        CreateSettingRow(content, width_group, "support_material_interface_extrusion_width",
                         _L("Support material interface"));
        CreateSettingRow(content, width_group, "automatic_extrusion_widths", _L("Automatic extrusion widths"));
        sizer->Add(width_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Overlap group
    if (has_any_visible_setting({"external_perimeter_overlap", "perimeter_perimeter_overlap", "infill_overlap",
                                 "bridge_infill_perimeter_overlap", "bridge_infill_overlap"}))
    {
        auto *overlap_group = CreateFlatStaticBoxSizer(content, _L("Overlap"));
        CreateSettingRow(content, overlap_group, "external_perimeter_overlap", _L("External perimeter overlap"));
        CreateSettingRow(content, overlap_group, "perimeter_perimeter_overlap", _L("Perimeter overlap"));
        CreateSettingRow(content, overlap_group, "infill_overlap", _L("Infill/perimeters overlap"));
        CreateSettingRow(content, overlap_group, "bridge_infill_perimeter_overlap",
                         _L("Bridge infill perimeter overlap"));
        CreateSettingRow(content, overlap_group, "bridge_infill_overlap", _L("Bridge infill overlap"));
        sizer->Add(overlap_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Flow group
    if (has_any_visible_setting({"bridge_flow_ratio"}))
    {
        auto *flow_group = CreateFlatStaticBoxSizer(content, _L("Flow"));
        CreateSettingRow(content, flow_group, "bridge_flow_ratio", _L("Bridge flow ratio"));
        sizer->Add(flow_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Slicing group
    if (has_any_visible_setting({"slice_closing_radius", "slicing_mode", "resolution", "gcode_resolution",
                                 "xy_size_compensation", "elefant_foot_compensation"}))
    {
        auto *slice_group = CreateFlatStaticBoxSizer(content, _L("Slicing"));
        CreateSettingRow(content, slice_group, "slice_closing_radius", _L("Slice closing radius"));
        CreateSettingRow(content, slice_group, "slicing_mode", _L("Slicing mode"));
        CreateSettingRow(content, slice_group, "resolution", _L("Resolution"));
        CreateSettingRow(content, slice_group, "gcode_resolution", _L("G-code resolution"));
        CreateSettingRow(content, slice_group, "xy_size_compensation", _L("XY size compensation"));
        CreateSettingRow(content, slice_group, "elefant_foot_compensation", _L("Elephant foot compensation"));
        sizer->Add(slice_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Athena / Arachne perimeter generator group
    if (has_any_visible_setting({"perimeter_compression", "wall_transition_angle", "wall_transition_filter_deviation",
                                 "wall_transition_length", "wall_distribution_count", "min_bead_width",
                                 "min_feature_size"}))
    {
        auto *arachne_group = CreateFlatStaticBoxSizer(content, _L("Athena / Arachne perimeter generator"));
        CreateSettingRow(content, arachne_group, "perimeter_compression", _L("Perimeter compression"));
        CreateSettingRow(content, arachne_group, "wall_transition_angle", _L("Wall transition angle"));
        CreateSettingRow(content, arachne_group, "wall_transition_filter_deviation",
                         _L("Wall transition filter deviation"));
        CreateSettingRow(content, arachne_group, "wall_transition_length", _L("Wall transition length"));
        CreateSettingRow(content, arachne_group, "wall_distribution_count", _L("Wall distribution count"));
        CreateSettingRow(content, arachne_group, "min_bead_width", _L("Min bead width"));
        CreateSettingRow(content, arachne_group, "min_feature_size", _L("Min feature size"));
        sizer->Add(arachne_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *PrintSettingsPanel::BuildOutputContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Sequential printing group
    if (has_any_visible_setting({"complete_objects"}))
    {
        auto *seq_group = CreateFlatStaticBoxSizer(content, _L("Sequential printing"));
        CreateSettingRow(content, seq_group, "complete_objects", _L("Complete individual objects"));
        sizer->Add(seq_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Output file group
    if (has_any_visible_setting({"gcode_comments", "gcode_label_objects", "output_filename_format"}))
    {
        auto *output_group = CreateFlatStaticBoxSizer(content, _L("Output file"));
        CreateSettingRow(content, output_group, "gcode_comments", _L("Verbose G-code"));
        CreateSettingRow(content, output_group, "gcode_label_objects", _L("Label objects"));
        CreateSettingRow(content, output_group, "output_filename_format", _L("Output filename format"), true);
        sizer->Add(output_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

void PrintSettingsPanel::CreateMultilineSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                                   const wxString &label, int num_lines)
{
    // Check sidebar visibility - skip if user has hidden this setting
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        return;

    int em = wxGetApp().em_unit();

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : from_u8(opt_def->tooltip);

    // Label row with icons
    auto *label_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    auto *lock_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("lock_closed"));
    lock_icon->SetMinSize(GetScaledIconSizeWx());
    lock_icon->SetBackgroundColour(bg_color);
    lock_icon->SetToolTip(_L("Value is same as in the system preset"));
    label_sizer->Add(lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    auto *undo_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("dot"));
    undo_icon->SetMinSize(GetScaledIconSizeWx());
    undo_icon->SetBackgroundColour(bg_color);
    label_sizer->Add(undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    wxString label_with_colon = label + ":";
    auto *label_text = new wxStaticText(parent, wxID_ANY, label_with_colon);
    label_text->SetBackgroundColour(bg_color);
    if (!tooltip.empty())
        label_text->SetToolTip(tooltip);
    label_sizer->Add(label_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

    sizer->Add(label_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 8);

    // Text control
    auto *text = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, num_lines * em * 1.5),
                                wxTE_MULTILINE | wxBORDER_SIMPLE);
    text->SetMinSize(wxSize(-1, num_lines * em * 1.5));
    if (!tooltip.empty())
        text->SetToolTip(tooltip);

    const DynamicPrintConfig &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    std::string original_value;
    if (config.has(opt_key))
    {
        text->SetValue(from_u8(config.opt_serialize(opt_key)));
        original_value = config.opt_serialize(opt_key);
    }

    text->Bind(wxEVT_TEXT, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

    sizer->Add(text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);

    // Store UI elements
    SettingUIElements ui_elem;
    ui_elem.control = text;
    ui_elem.lock_icon = lock_icon;
    ui_elem.undo_icon = undo_icon;
    ui_elem.original_value = original_value;
    m_setting_controls[opt_key] = ui_elem;

    UpdateUndoUI(opt_key);

    // Bind undo icon click
    undo_icon->Bind(wxEVT_LEFT_DOWN,
                    [this, opt_key](wxMouseEvent &)
                    {
                        auto it = m_setting_controls.find(opt_key);
                        if (it == m_setting_controls.end())
                            return;

                        if (auto *txt = dynamic_cast<wxTextCtrl *>(it->second.control))
                        {
                            txt->SetValue(from_u8(it->second.original_value));
                        }

                        OnSettingChanged(opt_key);
                        UpdateUndoUI(opt_key);
                    });
}

void PrintSettingsPanel::CreateSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                          const wxString &label, bool full_width)
{
    // Check sidebar visibility - skip if user has hidden this setting
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        return;

    int em = wxGetApp().em_unit();

    // Create the common row header (icons, label, sizers)
    RowUIContext ctx = CreateRowUIBase(parent, opt_key, label);
    if (!ctx.row_sizer)
        return; // Option not found

    const ConfigOptionDef *opt_def = ctx.opt_def;
    wxStaticBitmap *lock_icon = ctx.lock_icon;
    wxStaticBitmap *undo_icon = ctx.undo_icon;
    wxBoxSizer *row_sizer = ctx.row_sizer;
    wxString tooltip = ctx.tooltip;

    wxWindow *value_ctrl = nullptr;
    const DynamicPrintConfig &config = GetEditedConfig();
    std::string original_value;

    switch (opt_def->type)
    {
    case coBool:
    {
        // Value column sizer with proportion 1 for 50/50 split
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *checkbox = new ::CheckBox(parent);
        checkbox->SetBackgroundColour(SidebarColors::Background());
        if (config.has(opt_key))
        {
            checkbox->SetValue(config.opt_bool(opt_key));
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            checkbox->SetToolTip(tooltip);

        checkbox->Bind(wxEVT_CHECKBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
        value_sizer->AddStretchSpacer(1);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = checkbox;
        break;
    }

    case coEnum:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Use custom ComboBox widget for proper dark mode support
        // DD_NO_CHECK_ICON removes checkmarks, 16em width matches main tabs
        auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16 * em, -1), 0,
                                     nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
        if (opt_def->enum_def && opt_def->enum_def->has_labels())
        {
            for (const std::string &enum_label : opt_def->enum_def->labels())
            {
                combo->Append(from_u8(enum_label));
            }
        }

        if (config.has(opt_key) && opt_def->enum_def && opt_def->enum_def->has_values())
        {
            original_value = config.opt_serialize(opt_key);
            const auto &values = opt_def->enum_def->values();
            for (size_t idx = 0; idx < values.size(); ++idx)
            {
                if (values[idx] == original_value)
                {
                    combo->SetSelection(static_cast<int>(idx));
                    break;
                }
            }
        }
        if (!tooltip.empty())
            combo->SetToolTip(tooltip);

        combo->Bind(wxEVT_COMBOBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(combo, 0, wxALIGN_CENTER_VERTICAL); // Fixed 16em width (matches main tabs)
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = combo;
        break;
    }

    case coInt:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        int min_val = opt_def->min > INT_MIN ? static_cast<int>(opt_def->min) : 0;
        int max_val = opt_def->max < INT_MAX ? static_cast<int>(opt_def->max) : 10000;
        int value = 0;
        if (config.has(opt_key))
        {
            value = config.opt_int(opt_key);
            original_value = config.opt_serialize(opt_key);
        }

        wxString text_value = wxString::Format("%d", value);

        // Simple creation - 70px width like Tab.cpp, left aligned
        auto *spin = new SpinInput(parent, text_value, "", wxDefaultPosition, wxSize(GetScaledInputWidth(), -1), 0,
                                   min_val, max_val, value);

        if (opt_def->step > 1)
            spin->SetStep(static_cast<int>(opt_def->step));

        if (!tooltip.empty())
            spin->SetToolTip(tooltip);

        spin->Bind(wxEVT_SPINCTRL, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = spin;
        break;
    }

    case coFloat:
    case coFloatOrPercent:
    case coPercent:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition,
                                     wxSize(GetScaledInputWidth(), -1));
        wxGetApp().UpdateDarkUI(text);

        if (config.has(opt_key))
        {
            wxString value_str = from_u8(config.opt_serialize(opt_key));
            text->SetValue(value_str);
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL);

        // Add sidetext (units) if available - strip parenthetical notes for compact display
        if (!opt_def->sidetext.empty())
        {
            std::string sidetext = opt_def->sidetext;
            size_t paren_pos = sidetext.find('(');
            if (paren_pos != std::string::npos)
                sidetext = sidetext.substr(0, paren_pos);
            boost::trim(sidetext);
            if (!sidetext.empty())
            {
                auto *unit_text = new wxStaticText(parent, wxID_ANY, from_u8(sidetext));
                value_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            }
        }

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }

    case coString:
    case coStrings:
    default:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, wxDefaultSize);
        text->SetMinSize(wxSize(1, -1)); // Allow text to shrink
        wxGetApp().UpdateDarkUI(text);

        if (config.has(opt_key))
        {
            wxString value_str = from_u8(config.opt_serialize(opt_key));
            text->SetValue(value_str);
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 1, wxEXPAND); // Always shrink with sizer
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }
    }

    if (value_ctrl)
    {
        SettingUIElements ui_elem;
        ui_elem.control = value_ctrl;
        ui_elem.lock_icon = lock_icon;
        ui_elem.undo_icon = undo_icon;
        ui_elem.label_text = ctx.label_text;
        ui_elem.original_value = original_value;
        m_setting_controls[opt_key] = ui_elem;

        // Set initial icon state based on system preset comparison
        UpdateUndoUI(opt_key);

        // Bind undo icon click to revert value
        undo_icon->Bind(wxEVT_LEFT_DOWN,
                        [this, opt_key](wxMouseEvent &)
                        {
                            auto it = m_setting_controls.find(opt_key);
                            if (it == m_setting_controls.end())
                                return;

                            const ConfigOptionDef *def = print_config_def.get(opt_key);
                            if (!def)
                                return;

                            // Revert to original value
                            switch (def->type)
                            {
                            case coBool:
                                if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
                                {
                                    cb->SetValue(it->second.original_value == "1");
                                }
                                break;
                            case coInt:
                                if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
                                {
                                    spin->SetValue(std::stoi(it->second.original_value));
                                }
                                break;
                            case coEnum:
                                if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
                                {
                                    if (def->enum_def && def->enum_def->has_values())
                                    {
                                        const auto &values = def->enum_def->values();
                                        for (size_t idx = 0; idx < values.size(); ++idx)
                                        {
                                            if (values[idx] == it->second.original_value)
                                            {
                                                combo->SetSelection(static_cast<int>(idx));
                                                break;
                                            }
                                        }
                                    }
                                }
                                break;
                            default:
                                if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
                                {
                                    text_input->SetValue(from_u8(it->second.original_value));
                                }
                                else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
                                {
                                    text->SetValue(from_u8(it->second.original_value));
                                }
                                break;
                            }

                            OnSettingChanged(opt_key);
                            UpdateUndoUI(opt_key);
                        });
    }

    sizer->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
}

void PrintSettingsPanel::OnSettingChanged(const std::string &opt_key)
{
    // Prevent cascading events during RefreshFromConfig or validation
    if (m_disable_update)
        return;

    // Set flag to prevent re-entrancy (e.g., when validation dialog steals focus)
    m_disable_update = true;

    auto it = m_setting_controls.find(opt_key);
    if (it == m_setting_controls.end())
        return;

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    // Get the new value from the control
    DynamicPrintConfig &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;

    switch (opt_def->type)
    {
    case coBool:
        if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
        {
            config.set_key_value(opt_key, new ConfigOptionBool(cb->GetValue()));
        }
        break;
    case coInt:
        if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
        {
            config.set_key_value(opt_key, new ConfigOptionInt(spin->GetValue()));
        }
        break;
    case coEnum:
        if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
        {
            int sel = combo->GetSelection();
            if (sel != wxNOT_FOUND && opt_def->enum_def && opt_def->enum_def->has_values())
            {
                const auto &values = opt_def->enum_def->values();
                if (sel < static_cast<int>(values.size()))
                {
                    // Need to set the enum by its string value
                    config.set_deserialize_strict(opt_key, values[sel]);
                }
            }
        }
        break;
    case coFloat:
    case coFloatOrPercent:
    case coPercent:
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            std::string value_str = into_u8(text_input->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            std::string value_str = into_u8(text->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        break;
    case coString:
    case coStrings:
    default:
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            std::string value_str = into_u8(text_input->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            std::string value_str = into_u8(text->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        break;
    }

    // Run validation through ConfigManipulation (same as Tab.cpp)
    // Use mainframe as dialog parent for proper centering
    ConfigManipulation config_manipulation(
        [this]()
        {
            // load_config callback - refresh all controls from config
            RefreshFromConfig();
        },
        nullptr,             // cb_toggle_field - not needed for sidebar
        nullptr,             // cb_value_change - not needed
        nullptr,             // local_config
        wxGetApp().mainframe // msg_dlg_parent - center dialogs on main window
    );
    // Pass the changed key so validation only runs for relevant changes
    config_manipulation.update_print_fff_config(&config, true, opt_key);

    // Update the Print Settings tab to reflect the change
    // Note: Don't call update() as it runs validation again
    Tab *print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    if (print_tab)
    {
        print_tab->reload_config();
        print_tab->update_dirty();
        print_tab->update_changed_ui();
    }

    // Update UI state
    UpdateUndoUI(opt_key);

    // Schedule background slicing
    if (GetPlater())
    {
        GetPlater()->schedule_background_process();
    }

    // Apply toggle logic to enable/disable dependent options
    ApplyToggleLogic();

    // UNIFIED THEMING: Call SysColorsChanged on parent controls (TextInput, SpinInput, ComboBox)
    // These controls contain ThemedTextCtrl and handle their own color management via WM_CTLCOLOREDIT
    for (auto &[key, ui_elem] : m_setting_controls)
    {
        // Check for our custom controls first - they handle their own theming
        if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
        {
            // Plain wxTextCtrl (not inside our custom controls) - apply colors directly
#ifdef _WIN32
            bool is_editable = text->IsEditable();
            // Disable visual styles so SetBackgroundColour works properly
            SetWindowTheme(text->GetHWND(), L"", L"");
            text->SetBackgroundColour(is_editable ? SidebarColors::InputBackground()
                                                  : SidebarColors::DisabledBackground());
            text->SetForegroundColour(is_editable ? SidebarColors::InputForeground()
                                                  : SidebarColors::DisabledForeground());
            RedrawWindow(text->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
#endif
        }
    }

    // Re-enable updates
    m_disable_update = false;
}

void PrintSettingsPanel::UpdateUndoUI(const std::string &opt_key)
{
    auto it = m_setting_controls.find(opt_key);
    if (it != m_setting_controls.end())
        UpdateUndoUICommon(opt_key, it->second.undo_icon, it->second.lock_icon, it->second.original_value);
}

void PrintSettingsPanel::RefreshFromConfig()
{
    // RAII guard prevents flag from getting stuck if exception occurs
    DisableUpdateGuard guard(m_disable_update);

    try
    {
        const DynamicPrintConfig &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        for (auto &[opt_key, ui_elem] : m_setting_controls)
        {
            if (!config.has(opt_key))
                continue;

            const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
            if (!opt_def)
                continue;

            // Note: Do NOT update original_value here - it should only be set when
            // the control is created or when a preset is loaded/saved

            switch (opt_def->type)
            {
            case coBool:
            {
                if (auto *cb = dynamic_cast<::CheckBox *>(ui_elem.control))
                {
                    cb->SetValue(config.opt_bool(opt_key));
                }
                break;
            }
            case coInt:
            {
                if (auto *spin = dynamic_cast<SpinInput *>(ui_elem.control))
                {
                    spin->SetValue(config.opt_int(opt_key));
                }
                break;
            }
            case coEnum:
            {
                if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
                {
                    if (opt_def->enum_def && opt_def->enum_def->has_values())
                    {
                        std::string current_val = config.opt_serialize(opt_key);
                        const auto &values = opt_def->enum_def->values();
                        for (size_t idx = 0; idx < values.size(); ++idx)
                        {
                            if (values[idx] == current_val)
                            {
                                combo->SetSelection(static_cast<int>(idx));
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case coFloat:
            case coFloatOrPercent:
            case coPercent:
            case coFloats:
            case coPercents:
            case coString:
            case coStrings:
            {
                if (auto *text_input = dynamic_cast<::TextInput *>(ui_elem.control))
                {
                    text_input->SetValue(from_u8(config.opt_serialize(opt_key)));
                }
                else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
                {
                    text->SetValue(from_u8(config.opt_serialize(opt_key)));
                }
                break;
            }
            default:
                break;
            }

            // Reset undo UI to unmodified state
            UpdateUndoUI(opt_key);
        }
    }
    catch (const std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << "PrintSettingsPanel::RefreshFromConfig exception: " << e.what();
    }
    catch (...)
    {
        BOOST_LOG_TRIVIAL(error) << "PrintSettingsPanel::RefreshFromConfig unknown exception";
    }

    // Note: m_disable_update is reset by DisableUpdateGuard destructor

    // Apply toggle logic after refreshing values
    ApplyToggleLogic();
}

void PrintSettingsPanel::ToggleOption(const std::string &opt_key, bool enable)
{
    auto it = m_setting_controls.find(opt_key);
    if (it != m_setting_controls.end())
        ToggleOptionControl(it->second.control, enable);
}

void PrintSettingsPanel::ApplyToggleLogic()
{
    // Get current config - mirrors ConfigManipulation::toggle_print_fff_options()
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;

    // Perimeter dependencies
    bool have_perimeters = config.opt_int("perimeters") > 0;
    for (const char *el :
         {"extra_perimeters", "extra_perimeters_on_overhangs", "thin_walls", "overhangs", "seam_position",
          "staggered_inner_seams", "external_perimeters_first", "external_perimeter_extrusion_width", "perimeter_speed",
          "small_perimeter_speed", "external_perimeter_speed", "enable_dynamic_overhang_speeds"})
        ToggleOption(el, have_perimeters);

    // Dynamic overhang speeds depend on enable_dynamic_overhang_speeds
    bool have_dynamic_overhang = have_perimeters && config.opt_bool("enable_dynamic_overhang_speeds");
    for (int i = 0; i < 4; i++)
        ToggleOption("overhang_speed_" + std::to_string(i), have_dynamic_overhang);

    // Infill dependencies
    bool have_infill = config.option<ConfigOptionPercent>("fill_density")->value > 0;
    bool has_automatic_infill_combination = config.option<ConfigOptionBool>("automatic_infill_combination")->value;
    for (const char *el : {"fill_pattern", "solid_infill_every_layers", "solid_infill_below_area", "infill_extruder",
                           "infill_anchor_max", "automatic_infill_combination"})
        ToggleOption(el, have_infill);

    ToggleOption("infill_every_layers", have_infill && !has_automatic_infill_combination);
    ToggleOption("automatic_infill_combination_max_layer_height", have_infill && has_automatic_infill_combination);

    bool has_infill_anchors = have_infill && config.option<ConfigOptionFloatOrPercent>("infill_anchor_max")->value > 0;
    ToggleOption("infill_anchor", has_infill_anchors);

    // Solid infill dependencies
    bool has_spiral_vase = config.opt_bool("spiral_vase");
    bool has_top_solid_infill = config.opt_int("top_solid_layers") > 0;
    bool has_bottom_solid_infill = config.opt_int("bottom_solid_layers") > 0;
    bool has_solid_infill = has_top_solid_infill || has_bottom_solid_infill;

    for (const char *el : {"top_fill_pattern", "bottom_fill_pattern", "infill_first", "solid_infill_extruder",
                           "solid_infill_extrusion_width", "solid_infill_speed"})
        ToggleOption(el, has_solid_infill);

    for (const char *el :
         {"fill_angle", "bridge_angle", "infill_extrusion_width", "infill_speed", "bridge_speed", "over_bridge_speed"})
        ToggleOption(el, have_infill || has_solid_infill);

    bool has_narrow_solid_concentric = config.opt_bool("narrow_solid_infill_concentric");
    ToggleOption("narrow_solid_infill_threshold", has_narrow_solid_concentric);

    bool has_ensure_vertical_shell_thickness = config.opt_enum<EnsureVerticalShellThickness>(
                                                   "ensure_vertical_shell_thickness") !=
                                               EnsureVerticalShellThickness::Disabled;
    ToggleOption("top_solid_min_thickness",
                 !has_spiral_vase && has_top_solid_infill && has_ensure_vertical_shell_thickness);
    ToggleOption("bottom_solid_min_thickness",
                 !has_spiral_vase && has_bottom_solid_infill && has_ensure_vertical_shell_thickness);

    ToggleOption("gap_fill_speed", have_perimeters);

    // Fuzzy skin dependencies
    FuzzySkinNoiseType noise_type = config.opt_enum<FuzzySkinNoiseType>("fuzzy_skin_noise_type");
    bool have_structured_noise = noise_type != FuzzySkinNoiseType::Classic;
    ToggleOption("fuzzy_skin_scale", have_structured_noise);
    bool have_octaves = have_structured_noise && noise_type != FuzzySkinNoiseType::Voronoi;
    ToggleOption("fuzzy_skin_octaves", have_octaves);
    bool have_persistence = have_structured_noise &&
                            (noise_type == FuzzySkinNoiseType::Perlin || noise_type == FuzzySkinNoiseType::Billow);
    ToggleOption("fuzzy_skin_persistence", have_persistence);

    // Interlocking perimeters dependencies
    bool interlock_enabled = config.opt_bool("interlock_perimeters_enabled");
    ToggleOption("interlock_perimeter_count", interlock_enabled);
    ToggleOption("interlock_perimeter_overlap", interlock_enabled);
    ToggleOption("interlock_flow_detection", interlock_enabled);

    // Top surface flow dependencies
    bool has_top_surface_flow_reduction = config.option<ConfigOptionPercent>("top_surface_flow_reduction")->value > 0;
    ToggleOption("top_surface_visibility_detection", has_top_surface_flow_reduction);

    for (const char *el : {"top_infill_extrusion_width", "top_solid_infill_speed"})
        ToggleOption(el, has_top_solid_infill || (has_spiral_vase && has_bottom_solid_infill));

    // Acceleration dependencies
    bool have_default_acceleration = config.opt_float("default_acceleration") > 0;
    for (const char *el : {"perimeter_acceleration", "infill_acceleration", "top_solid_infill_acceleration",
                           "solid_infill_acceleration", "external_perimeter_acceleration", "bridge_acceleration",
                           "first_layer_acceleration", "wipe_tower_acceleration"})
        ToggleOption(el, have_default_acceleration);

    // Skirt dependencies
    bool have_skirt = config.opt_int("skirts") > 0;
    ToggleOption("skirt_height", have_skirt && config.opt_enum<DraftShield>("draft_shield") != dsEnabled);
    for (const char *el : {"skirt_distance", "draft_shield", "min_skirt_length"})
        ToggleOption(el, have_skirt);

    // Brim dependencies
    bool have_brim = config.opt_enum<BrimType>("brim_type") != btNoBrim;
    for (const char *el : {"brim_width", "brim_separation", "brim_ears_max_angle", "brim_ears_detection_length"})
        ToggleOption(el, have_brim);
    ToggleOption("perimeter_extruder", have_perimeters || have_brim);

    // Support material dependencies
    bool have_raft = config.opt_int("raft_layers") > 0;
    bool have_support_material = config.opt_bool("support_material") || have_raft;
    bool have_support_material_auto = have_support_material && config.opt_bool("support_material_auto");
    bool have_support_interface = config.opt_int("support_material_interface_layers") > 0;
    bool have_support_soluble = have_support_material &&
                                config.opt_enum<SupportTopContactGap>("support_material_contact_distance") == stcgNoGap;

    for (const char *el :
         {"support_material_pattern", "support_material_with_sheath", "support_material_spacing",
          "support_material_angle", "support_material_interface_pattern", "support_material_interface_layers",
          "dont_support_bridges", "support_material_contact_distance", "support_material_xy_spacing"})
        ToggleOption(el, have_support_material);

    ToggleOption("support_material_style", have_support_material_auto);
    ToggleOption("support_material_threshold", have_support_material_auto);
    ToggleOption("support_material_bottom_contact_distance", have_support_material);

    bool have_custom_top_gap = have_support_material && !have_support_soluble &&
                               config.opt_enum<SupportTopContactGap>("support_material_contact_distance") == stcgCustom;
    ToggleOption("support_material_contact_distance_custom", have_custom_top_gap);

    bool have_half_layer_gap = have_support_material &&
                               config.opt_enum<SupportBottomContactGap>("support_material_bottom_contact_distance") ==
                                   sbcgHalfLayer;
    ToggleOption("support_material_bottom_contact_extrusion_width", have_half_layer_gap);

    ToggleOption("support_material_closing_radius", have_support_material);
    ToggleOption("support_material_min_area", have_support_material);

    // Organic supports - available when any support is enabled
    bool has_organic_supports = config.opt_bool("support_material") ||
                                config.opt_int("support_material_enforce_layers") > 0;
    for (const char *key : {"support_tree_angle", "support_tree_angle_slow", "support_tree_branch_diameter",
                            "support_tree_branch_diameter_angle", "support_tree_branch_diameter_double_wall",
                            "support_tree_tip_diameter", "support_tree_branch_distance", "support_tree_top_rate"})
        ToggleOption(key, has_organic_supports);

    for (const char *el : {"support_material_bottom_interface_layers", "support_material_interface_spacing",
                           "support_material_interface_extruder", "support_material_interface_speed",
                           "support_material_interface_contact_loops"})
        ToggleOption(el, have_support_material && have_support_interface);

    ToggleOption("perimeter_extrusion_width", have_perimeters || have_skirt || have_brim);
    ToggleOption("support_material_extruder", have_support_material || have_skirt);
    ToggleOption("support_material_speed", have_support_material || have_brim || have_skirt);

    // Raft dependencies
    ToggleOption("raft_contact_distance", have_raft && !have_support_soluble);
    for (const char *el : {"raft_expansion", "first_layer_acceleration_over_raft", "first_layer_speed_over_raft"})
        ToggleOption(el, have_raft);

    // Ironing dependencies
    bool has_ironing = config.opt_bool("ironing");
    for (const char *el : {"ironing_type", "ironing_flowrate", "ironing_spacing", "ironing_speed"})
        ToggleOption(el, has_ironing);

    // Ooze prevention dependencies
    bool have_ooze_prevention = config.opt_bool("ooze_prevention");
    ToggleOption("standby_temperature_delta", have_ooze_prevention);

    // Wipe tower dependencies
    bool have_wipe_tower = config.opt_bool("wipe_tower");
    for (const char *el : {"wipe_tower_width", "wipe_tower_brim_width", "wipe_tower_cone_angle",
                           "wipe_tower_extra_spacing", "wipe_tower_extra_flow", "wipe_tower_bridging",
                           "wipe_tower_no_sparse_layers", "single_extruder_multi_material_priming"})
        ToggleOption(el, have_wipe_tower);

    // Avoid crossing dependencies - mutually exclusive
    ToggleOption("avoid_crossing_curled_overhangs", !config.opt_bool("avoid_crossing_perimeters"));
    ToggleOption("avoid_crossing_perimeters", !config.opt_bool("avoid_crossing_curled_overhangs"));
    bool have_avoid_crossing_perimeters = config.opt_bool("avoid_crossing_perimeters");
    ToggleOption("avoid_crossing_perimeters_max_detour", have_avoid_crossing_perimeters);

    // Perimeter generator dependencies
    bool have_arachne = config.opt_enum<PerimeterGeneratorType>("perimeter_generator") ==
                        PerimeterGeneratorType::Arachne;
    bool have_athena = config.opt_enum<PerimeterGeneratorType>("perimeter_generator") == PerimeterGeneratorType::Athena;
    bool have_advanced_perimeters = have_arachne || have_athena;

    ToggleOption("wall_transition_length", have_advanced_perimeters);
    ToggleOption("wall_transition_filter_deviation", have_advanced_perimeters);
    ToggleOption("wall_transition_angle", have_advanced_perimeters);
    ToggleOption("wall_distribution_count", have_arachne);
    ToggleOption("min_feature_size", have_advanced_perimeters);
    ToggleOption("min_bead_width", have_arachne);
    ToggleOption("perimeter_compression", have_athena);

    // Scarf seam dependencies
    ToggleOption("scarf_seam_placement", !has_spiral_vase);
    auto scarf_seam_placement = config.opt_enum<ScarfSeamPlacement>("scarf_seam_placement");
    bool uses_scarf_seam = !has_spiral_vase && scarf_seam_placement != ScarfSeamPlacement::nowhere;
    for (const char *el : {"scarf_seam_only_on_smooth", "scarf_seam_start_height", "scarf_seam_entire_loop",
                           "scarf_seam_length", "scarf_seam_max_segment_length", "scarf_seam_on_inner_perimeters"})
        ToggleOption(el, uses_scarf_seam);

    // Interlocking beam dependencies
    bool use_beam_interlocking = config.opt_bool("interlocking_beam");
    for (const char *el : {"interlocking_beam_width", "interlocking_orientation", "interlocking_beam_layer_count",
                           "interlocking_depth", "interlocking_boundary_avoidance"})
        ToggleOption(el, use_beam_interlocking);
    ToggleOption("mmu_segmented_region_max_width", !use_beam_interlocking);

    bool have_non_zero_mmu_segmented_region_max_width = !use_beam_interlocking &&
                                                        config.opt_float("mmu_segmented_region_max_width") > 0.;
    ToggleOption("mmu_segmented_region_interlocking_depth", have_non_zero_mmu_segmented_region_max_width);
}

void PrintSettingsPanel::msw_rescale()
{
    // Update icon sizes and rescale controls for DPI scaling
    wxSize icon_size = GetScaledIconSizeWx();
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (ui_elem.lock_icon)
            ui_elem.lock_icon->SetMinSize(icon_size);
        if (ui_elem.undo_icon)
            ui_elem.undo_icon->SetMinSize(icon_size);
        // Rescale SpinInput controls so internal buttons reposition correctly
        if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->Rescale();
    }

    // Base class handles tab headers and layout
    TabbedSettingsPanel::msw_rescale();
}

// Helper to recursively update all ScalableButtons in a window hierarchy
static void UpdateScalableButtonsRecursive(wxWindow *window)
{
    if (!window)
        return;

    // Check if this window is a ScalableButton
    if (auto *btn = dynamic_cast<ScalableButton *>(window))
        btn->sys_color_changed();

    // Recursively process children
    for (wxWindow *child : window->GetChildren())
        UpdateScalableButtonsRecursive(child);
}

void PrintSettingsPanel::sys_color_changed()
{
    // Base class handles panel backgrounds and tab headers
    TabbedSettingsPanel::sys_color_changed();

    // Get current theme background color
    wxColour bg_color = SidebarColors::Background();

    // Refresh ALL setting controls for the new theme
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (!ui_elem.control)
            continue;

        // Update label background color
        if (ui_elem.label_text)
            ui_elem.label_text->SetBackgroundColour(bg_color);

        // Update icon background colors
        if (ui_elem.lock_icon)
            ui_elem.lock_icon->SetBackgroundColour(bg_color);
        if (ui_elem.undo_icon)
            ui_elem.undo_icon->SetBackgroundColour(bg_color);

        // Handle all custom widget types that have SysColorsChanged/sys_color_changed
        if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *checkbox = dynamic_cast<::CheckBox *>(ui_elem.control))
            checkbox->sys_color_changed();

        UpdateUndoUI(opt_key);
    }

    // Update all ScalableButtons
    UpdateScalableButtonsRecursive(this);
}

// ============================================================================
// PrinterSettingsPanel Implementation - Printer settings with tabbed categories
// ============================================================================

PrinterSettingsPanel::PrinterSettingsPanel(wxWindow *parent, Plater *plater)
    : TabbedSettingsPanel(parent, plater), m_extruders_count(1)
{
    // Get actual extruder count from config
    if (wxGetApp().preset_bundle)
    {
        const auto *nozzle_opt =
            wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
        if (nozzle_opt)
            m_extruders_count = nozzle_opt->values.size();
    }
    BuildUI();
}

PrinterSettingsPanel::~PrinterSettingsPanel()
{
    // Invalidate the alive flag to prevent pending CallAfter callbacks from executing
    // This prevents use-after-free crashes if the panel is destroyed while a callback is pending
    *m_prevent_call_after_crash = false;
}

DynamicPrintConfig &PrinterSettingsPanel::GetEditedConfig()
{
    return wxGetApp().preset_bundle->printers.get_edited_preset().config;
}

const DynamicPrintConfig &PrinterSettingsPanel::GetEditedConfig() const
{
    return wxGetApp().preset_bundle->printers.get_edited_preset().config;
}

const Preset *PrinterSettingsPanel::GetSystemPresetParent() const
{
    return wxGetApp().preset_bundle->printers.get_selected_preset_parent();
}

Tab *PrinterSettingsPanel::GetSyncTab() const
{
    return wxGetApp().get_tab(Preset::TYPE_PRINTER);
}

std::vector<TabbedSettingsPanel::TabDefinition> PrinterSettingsPanel::GetTabDefinitions()
{
    std::vector<TabDefinition> tabs = {{"general", _L("General"), "printer"}, {"limits", _L("Machine limits"), "cog"}};

    // Add extruder tabs dynamically
    for (size_t i = 0; i < m_extruders_count; ++i)
    {
        wxString name = wxString::Format("extruder_%zu", i);
        wxString title = m_extruders_count == 1 ? _L("Extruder") : wxString::Format(_L("Extruder %zu"), i + 1);
        tabs.push_back({name, title, "funnel"});
    }

    // Add Single extruder MM tab after extruder tabs (to match main settings order)
    if (ShouldShowSingleExtruderMM())
        tabs.push_back({"single_extruder_mm", _L("Single extruder MM"), "printer"});

    return tabs;
}

bool PrinterSettingsPanel::IsTabVisible(int tab_index) const
{
    // Tab layout:
    // 0: General
    // 1: Machine limits
    // 2 to 2+m_extruders_count-1: Extruder tabs
    // Last (if single_extruder_multi_material): Single extruder MM

    if (tab_index == 0) // General
    {
        // General tab has some unwrapped groups (Size and coordinates, Capabilities)
        // that are always shown, plus these wrapped groups
        return has_any_visible_setting({"gcode_flavor", "thumbnails", "silent_mode", "remaining_times", "binary_gcode",
                                        "use_relative_e_distances", "use_firmware_retraction", "use_volumetric_e",
                                        "variable_layer_height", "prefer_clockwise_movements",
                                        "extruder_clearance_radius", "extruder_clearance_height", "max_print_height",
                                        "z_offset", "single_extruder_multi_material"});
    }
    else if (tab_index == 1) // Machine limits
    {
        // Check gcode flavor to determine which settings are actually visible
        const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        auto flavor = static_cast<GCodeFlavor>(config.option("gcode_flavor")->getInt());
        bool is_rrf = (flavor == gcfRepRapFirmware || flavor == gcfRapid);

        // Check exactly what the content builder checks:
        // 1. General section (machine_limits_usage)
        if (has_any_visible_setting({"machine_limits_usage"}))
            return true;

        // 2. Marlin or RRF settings based on gcode flavor
        if (is_rrf)
        {
            // RRF mode - check RRF M-codes
            if (has_any_visible_setting({"machine_rrf_m566", "machine_rrf_m201", "machine_rrf_m203", "machine_rrf_m204",
                                         "machine_rrf_m207"}))
                return true;
        }
        else
        {
            // Marlin mode - check each Marlin group separately (mirroring content builder)
            if (has_any_visible_setting({"machine_max_feedrate_x", "machine_max_feedrate_y", "machine_max_feedrate_z",
                                         "machine_max_feedrate_e"}))
                return true;
            if (has_any_visible_setting({"machine_max_acceleration_x", "machine_max_acceleration_y",
                                         "machine_max_acceleration_z", "machine_max_acceleration_e",
                                         "machine_max_acceleration_extruding", "machine_max_acceleration_retracting",
                                         "machine_max_acceleration_travel"}))
                return true;
            if (has_any_visible_setting(
                    {"machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z", "machine_max_jerk_e"}))
                return true;
            if (has_any_visible_setting({"machine_max_junction_deviation"}))
                return true;
            if (has_any_visible_setting({"machine_min_extruding_rate", "machine_min_travel_rate"}))
                return true;
        }

        // 3. Time estimation section (machine_time_compensation)
        if (has_any_visible_setting({"machine_time_compensation"}))
            return true;

        return false;
    }
    else if (tab_index >= 2 && tab_index < 2 + static_cast<int>(m_extruders_count)) // Extruder tabs
    {
        // Check visibility for this specific extruder
        size_t extruder_idx = static_cast<size_t>(tab_index - 2);
        // Note: nozzle_diameter is not included - it's permanently shown in sidebar header
        return has_extruder_visible_setting({"extruder_colour",
                                             "fan_spinup_time",
                                             "fan_spinup_response_type",
                                             "min_layer_height",
                                             "max_layer_height",
                                             "extruder_offset",
                                             "retract_lift",
                                             "travel_ramping_lift",
                                             "travel_max_lift",
                                             "travel_slope",
                                             "travel_lift_before_obstacle",
                                             "retract_lift_above",
                                             "retract_lift_below",
                                             "retract_length",
                                             "retract_speed",
                                             "deretract_speed",
                                             "retract_restart_extra",
                                             "retract_before_wipe",
                                             "retract_before_travel",
                                             "retract_layer_change",
                                             "wipe",
                                             "wipe_extend",
                                             "wipe_length",
                                             "retract_length_toolchange",
                                             "retract_restart_extra_toolchange"},
                                            extruder_idx);
    }
    else // Single extruder MM tab (last tab when enabled)
    {
        return has_any_visible_setting({"cooling_tube_retraction", "cooling_tube_length", "parking_pos_retraction",
                                        "extra_loading_move", "multimaterial_purging",
                                        "high_current_on_filament_swap"});
    }
}

wxPanel *PrinterSettingsPanel::BuildTabContent(int tab_index)
{
    // Use tab name to determine content instead of fixed indices (since tabs can be conditional)
    if (tab_index < 0 || tab_index >= GetTabCount())
        return nullptr;

    const wxString &tab_name = GetTabName(tab_index);

    if (tab_name == "general")
        return BuildGeneralContent();
    else if (tab_name == "limits")
        return BuildMachineLimitsContent();
    else if (tab_name == "single_extruder_mm")
        return BuildSingleExtruderMMContent();
    else if (tab_name.StartsWith("extruder_"))
    {
        // Extract extruder index from name like "extruder_0", "extruder_1", etc.
        long extruder_idx = 0;
        tab_name.Mid(9).ToLong(&extruder_idx); // Skip "extruder_" prefix
        return BuildExtruderContent(static_cast<size_t>(extruder_idx));
    }
    return nullptr;
}

void PrinterSettingsPanel::UpdateExtruderCount(size_t count)
{
    if (count == m_extruders_count)
        return;

    // Prevent event handlers from firing during rebuild
    m_disable_update = true;
    m_extruders_count = count;

    // Defer rebuild to after current event processing completes
    // This prevents reentrancy issues where destroying UI components
    // while still processing their events corrupts the m_tabs vector
    // Capture alive flag by value (shared_ptr copy) to safely check if panel still exists
    auto alive = m_prevent_call_after_crash;
    CallAfter(
        [this, alive]()
        {
            if (!*alive)
                return; // Panel was destroyed, abort
            RebuildContent();
            m_disable_update = false;
        });
}

wxPanel *PrinterSettingsPanel::BuildGeneralContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Size and coordinates group
    auto *size_group = CreateFlatStaticBoxSizer(content, _L("Size and coordinates"));

    // Bed shape button - opens the existing bed shape dialog
    {
        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Left side: label (50% width)
        auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *label = new wxStaticText(content, wxID_ANY, _L("Bed shape:"), wxDefaultPosition, wxDefaultSize,
                                       wxST_ELLIPSIZE_END);
        label->SetMinSize(wxSize(1, -1));
        label->SetToolTip(_L("Shape and size of the print bed"));
        left_sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
        row_sizer->Add(left_sizer, 1, wxEXPAND);

        // Right side: button left-justified in value area (50% width)
        auto *right_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *btn = new ScalableButton(content, wxID_ANY, "settings", _L("Set bed shape"), wxDefaultSize,
                                       wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        btn->SetToolTip(_L("Open bed shape editor"));
        btn->Bind(wxEVT_BUTTON,
                  [](wxCommandEvent &)
                  {
                      DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
                      BedShapeDialog dlg(wxGetApp().mainframe);
                      dlg.build_dialog(*config.option<ConfigOptionPoints>("bed_shape"),
                                       *config.option<ConfigOptionString>("bed_custom_texture"),
                                       *config.option<ConfigOptionString>("bed_custom_model"));
                      dlg.CentreOnParent();
                      if (dlg.ShowModal() == wxID_OK)
                      {
                          const std::vector<Vec2d> &shape = dlg.get_shape();
                          const std::string &custom_texture = dlg.get_custom_texture();
                          const std::string &custom_model = dlg.get_custom_model();
                          if (!shape.empty())
                          {
                              config.set_key_value("bed_shape", new ConfigOptionPoints(shape));
                              config.set_key_value("bed_custom_texture", new ConfigOptionString(custom_texture));
                              config.set_key_value("bed_custom_model", new ConfigOptionString(custom_model));

                              wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);
                              if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                              {
                                  tab->reload_config();
                                  tab->update_dirty();
                                  tab->update_changed_ui();
                              }
                              if (wxGetApp().plater())
                                  wxGetApp().plater()->on_config_change(config);
                          }
                      }
                  });
        right_sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);
        right_sizer->AddStretchSpacer(1);
        row_sizer->Add(right_sizer, 1, wxEXPAND);

        size_group->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
    }

    CreateSettingRow(content, size_group, "max_print_height", _L("Max print height"));
    CreateSettingRow(content, size_group, "z_offset", _L("Z offset"));
    sizer->Add(size_group, 0, wxEXPAND | wxALL, em / 4);

    // Capabilities group
    auto *cap_group = CreateFlatStaticBoxSizer(content, _L("Capabilities"));

    // Extruders count - special handling (derived from nozzle_diameter array size)
    {
        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Set background color using unified accessor
        wxColour bg_color = SidebarColors::Background();

        // Lock icon - shows lock_closed when value matches system preset
        auto *lock_icon = new wxStaticBitmap(content, wxID_ANY, *get_bmp_bundle("lock_closed"));
        lock_icon->SetMinSize(GetScaledIconSizeWx());
        lock_icon->SetBackgroundColour(bg_color);
        lock_icon->SetToolTip(_L("Value is same as in the system preset"));
        left_sizer->Add(lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

        // Undo icon - shows dot when unchanged, undo arrow when modified
        auto *undo_icon = new wxStaticBitmap(content, wxID_ANY, *get_bmp_bundle("dot"));
        undo_icon->SetMinSize(GetScaledIconSizeWx());
        undo_icon->SetBackgroundColour(bg_color);
        left_sizer->Add(undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

        auto *label_text = new wxStaticText(content, wxID_ANY, _L("Extruders:"), wxDefaultPosition, wxDefaultSize,
                                            wxST_ELLIPSIZE_END);
        label_text->SetMinSize(wxSize(1, -1));
        label_text->SetBackgroundColour(bg_color);
        label_text->SetToolTip(_L("Number of extruders of the printer."));
        left_sizer->Add(label_text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
        row_sizer->Add(left_sizer, 1, wxEXPAND);

        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        int extruder_count = 1;
        std::string current_value;
        if (auto *nozzle_opt = config.option<ConfigOptionFloats>("nozzle_diameter"))
        {
            extruder_count = static_cast<int>(nozzle_opt->values.size());
            current_value = config.opt_serialize("nozzle_diameter");
        }

        // Use preserved original value if available (persists across rebuilds)
        // Otherwise use current config value as original
        std::string original_value;
        auto preserved_it = m_preserved_original_values.find("nozzle_diameter");
        if (preserved_it != m_preserved_original_values.end())
            original_value = preserved_it->second;
        else
            original_value = current_value;

        wxString text_value = wxString::Format("%d", extruder_count);

        // Simple creation - just like Tab.cpp does it (fixed 70px width)
        auto *spin = new SpinInput(content, text_value, "", wxDefaultPosition, wxSize(GetScaledInputWidth(), -1), 0, 1,
                                   256, extruder_count);
        spin->SetToolTip(_L("Number of extruders of the printer."));

        // Store UI elements for undo tracking (use nozzle_diameter as the key)
        SettingUIElements ui_elem;
        ui_elem.control = spin;
        ui_elem.lock_icon = lock_icon;
        ui_elem.undo_icon = undo_icon;
        ui_elem.original_value = original_value;
        m_setting_controls["nozzle_diameter"] = ui_elem;

        // Update undo UI to reflect current state
        UpdateUndoUI("nozzle_diameter");

        // Wire up undo icon click to revert
        undo_icon->Bind(wxEVT_LEFT_DOWN,
                        [this, spin](wxMouseEvent &)
                        {
                            auto it = m_setting_controls.find("nozzle_diameter");
                            if (it == m_setting_controls.end())
                                return;

                            // Revert to original value
                            DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
                            config.set_deserialize_strict("nozzle_diameter", it->second.original_value);

                            // Clear preserved original value since we're reverting
                            m_preserved_original_values.erase("nozzle_diameter");

                            // Update spin to show original count
                            if (auto *nozzle_opt = config.option<ConfigOptionFloats>("nozzle_diameter"))
                            {
                                int count = static_cast<int>(nozzle_opt->values.size());
                                spin->SetValue(count);
                                UpdateExtruderCount(static_cast<size_t>(count));
                            }

                            // Update undo UI
                            UpdateUndoUI("nozzle_diameter");

                            // Sync with tab - must call extruders_count_changed to properly rebuild
                            if (auto *nozzle_opt2 = config.option<ConfigOptionFloats>("nozzle_diameter"))
                            {
                                size_t count = nozzle_opt2->values.size();
                                if (auto *tab = dynamic_cast<TabPrinter *>(wxGetApp().get_tab(Preset::TYPE_PRINTER)))
                                {
                                    tab->extruders_count_changed(count);
                                    tab->update_dirty();
                                }
                            }
                        });

        spin->Bind(wxEVT_SPINCTRL,
                   [this, spin](wxCommandEvent &)
                   {
                       // Guard against events during rebuild
                       if (m_disable_update)
                           return;

                       int new_count = spin->GetValue();
                       if (new_count < 1)
                           new_count = 1;

                       DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;

                       // Resize nozzle_diameter array
                       auto *nozzle_opt = config.option<ConfigOptionFloats>("nozzle_diameter", true);
                       if (nozzle_opt)
                       {
                           std::vector<double> diameters = nozzle_opt->values;
                           double default_diameter = diameters.empty() ? 0.4 : diameters[0];
                           diameters.resize(static_cast<size_t>(new_count), default_diameter);
                           nozzle_opt->values = diameters;
                       }

                       // Resize other per-extruder options to match
                       static const std::vector<std::string> extruder_options = {"extruder_colour",
                                                                                 "extruder_offset",
                                                                                 "retract_length",
                                                                                 "retract_lift",
                                                                                 "retract_lift_above",
                                                                                 "retract_lift_below",
                                                                                 "retract_speed",
                                                                                 "deretract_speed",
                                                                                 "retract_restart_extra",
                                                                                 "retract_before_travel",
                                                                                 "retract_layer_change",
                                                                                 "retract_before_wipe",
                                                                                 "wipe",
                                                                                 "wipe_extend",
                                                                                 "wipe_length",
                                                                                 "retract_length_toolchange",
                                                                                 "retract_restart_extra_toolchange",
                                                                                 "min_layer_height",
                                                                                 "max_layer_height",
                                                                                 "fan_spinup_time",
                                                                                 "fan_spinup_response_type",
                                                                                 "travel_ramping_lift",
                                                                                 "travel_max_lift",
                                                                                 "travel_slope",
                                                                                 "travel_lift_before_obstacle"};

                       for (const std::string &opt_key : extruder_options)
                       {
                           ConfigOption *opt = config.option(opt_key, true);
                           if (opt)
                           {
                               auto *vec_opt = dynamic_cast<ConfigOptionVectorBase *>(opt);
                               if (vec_opt)
                                   vec_opt->resize(static_cast<size_t>(new_count));
                           }
                       }

                       // Mark preset as dirty
                       wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);

                       // Update undo UI for nozzle_diameter
                       UpdateUndoUI("nozzle_diameter");

                       // Update filament presets for the new extruder count (expands extruders_filaments vector)
                       wxGetApp().preset_bundle->update_multi_material_filament_presets();
                       wxGetApp().preset_bundle->update_filaments_compatible(
                           PresetSelectCompatibleType::OnlyIfWasCompatible);

                       // Sync with Printer Settings tab - must call extruders_count_changed
                       // to properly rebuild extruder pages
                       if (auto *tab = dynamic_cast<TabPrinter *>(wxGetApp().get_tab(Preset::TYPE_PRINTER)))
                       {
                           tab->extruders_count_changed(static_cast<size_t>(new_count));
                           tab->update_dirty();
                       }

                       // Preserve original value before rebuild (so undo button persists)
                       auto it = m_setting_controls.find("nozzle_diameter");
                       if (it != m_setting_controls.end() &&
                           m_preserved_original_values.find("nozzle_diameter") == m_preserved_original_values.end())
                       {
                           m_preserved_original_values["nozzle_diameter"] = it->second.original_value;
                       }

                       // Rebuild extruder tabs
                       UpdateExtruderCount(static_cast<size_t>(new_count));

                       // Trigger plater update - this will also trigger sidebar preset updates
                       if (GetPlater())
                           GetPlater()->on_config_change(config);
                   });
        value_sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        cap_group->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
    }

    CreateSettingRow(content, cap_group, "single_extruder_multi_material", _L("Single extruder multi material"));
    sizer->Add(cap_group, 0, wxEXPAND | wxALL, em / 4);

    // Firmware group
    if (has_any_visible_setting({"gcode_flavor", "thumbnails", "silent_mode", "remaining_times", "binary_gcode"}))
    {
        auto *fw_group = CreateFlatStaticBoxSizer(content, _L("Firmware"));
        CreateSettingRow(content, fw_group, "gcode_flavor", _L("G-code flavor"));
        CreateSettingRow(content, fw_group, "thumbnails", _L("G-code thumbnails"), true);
        CreateSettingRow(content, fw_group, "silent_mode", _L("Supports stealth mode"));
        CreateSettingRow(content, fw_group, "remaining_times", _L("Supports remaining times"));
        CreateSettingRow(content, fw_group, "binary_gcode", _L("Binary G-code"));
        sizer->Add(fw_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Advanced group
    if (has_any_visible_setting({"use_relative_e_distances", "use_firmware_retraction", "use_volumetric_e",
                                 "variable_layer_height", "prefer_clockwise_movements"}))
    {
        auto *adv_group = CreateFlatStaticBoxSizer(content, _L("Advanced"));
        CreateSettingRow(content, adv_group, "use_relative_e_distances", _L("Use relative E distances"));
        CreateSettingRow(content, adv_group, "use_firmware_retraction", _L("Use firmware retraction"));
        CreateSettingRow(content, adv_group, "use_volumetric_e", _L("Use volumetric E"));
        CreateSettingRow(content, adv_group, "variable_layer_height", _L("Supports variable layer height"));
        CreateSettingRow(content, adv_group, "prefer_clockwise_movements", _L("Prefer clockwise movements"));
        sizer->Add(adv_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Sequential printing limits group
    if (has_any_visible_setting({"extruder_clearance_radius", "extruder_clearance_height"}))
    {
        auto *seq_group = CreateFlatStaticBoxSizer(content, _L("Sequential printing limits"));
        CreateSettingRow(content, seq_group, "extruder_clearance_radius", _L("Extruder clearance radius"));
        CreateSettingRow(content, seq_group, "extruder_clearance_height", _L("Extruder clearance height"));
        sizer->Add(seq_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *PrinterSettingsPanel::BuildMachineLimitsContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // General group (common to both RRF and Marlin) - only show if machine_limits_usage is visible
    if (has_any_visible_setting({"machine_limits_usage"}))
    {
        auto *general_group = CreateFlatStaticBoxSizer(content, _L("General"));
        CreateSettingRow(content, general_group, "machine_limits_usage", _L("Machine limits usage"));

        // Filter "Emit to G-code" option for Klipper/RRF/Rapid on initial build
        // (ApplyToggleLogic handles this during runtime, but initial build needs it too)
        {
            const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
            const GCodeFlavor flavor = config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
            bool emit_to_gcode_available = (flavor != gcfKlipper && flavor != gcfRepRapFirmware && flavor != gcfRapid);

            auto usage_it = m_setting_controls.find("machine_limits_usage");
            if (usage_it != m_setting_controls.end())
            {
                if (auto *combo = dynamic_cast<::ComboBox *>(usage_it->second.control))
                {
                    if (!emit_to_gcode_available)
                    {
                        // Remove "Emit to G-code" option and repopulate
                        wxString current_value = combo->GetValue();
                        combo->Clear();
                        combo->Append(_L("Use for time estimate"));
                        combo->Append(_L("Ignore"));

                        // Find matching selection or default to "Use for time estimate"
                        int sel = wxNOT_FOUND;
                        for (unsigned int i = 0; i < combo->GetCount(); ++i)
                        {
                            if (combo->GetString(i) == current_value)
                            {
                                sel = static_cast<int>(i);
                                break;
                            }
                        }
                        if (sel == wxNOT_FOUND)
                            sel = 0; // "Use for time estimate"
                        combo->SetSelection(sel);
                    }
                }
            }
        }

        sizer->Add(general_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // ========== Marlin-style limits panel ==========
    m_marlin_limits_panel = new wxPanel(content, wxID_ANY);
    auto *marlin_sizer = new wxBoxSizer(wxVERTICAL);

    // Check if any Marlin settings are visible (for stealth note and overall panel content)
    bool has_any_marlin_visible = has_any_visible_setting(
        {"machine_max_feedrate_x", "machine_max_feedrate_y", "machine_max_feedrate_z", "machine_max_feedrate_e",
         "machine_max_acceleration_x", "machine_max_acceleration_y", "machine_max_acceleration_z",
         "machine_max_acceleration_e", "machine_max_acceleration_extruding", "machine_max_acceleration_retracting",
         "machine_max_acceleration_travel", "machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z",
         "machine_max_jerk_e", "machine_max_junction_deviation", "machine_min_extruding_rate",
         "machine_min_travel_rate"});

    // Stealth mode note (shown only when stealth mode is enabled AND Marlin settings visible)
    m_stealth_mode_note = nullptr;
    if (has_any_marlin_visible)
    {
        m_stealth_mode_note =
            new wxStaticText(m_marlin_limits_panel, wxID_ANY,
                             _L("Normal mode only. Edit Stealth in Printer Settings > Machine limits."));
        m_stealth_mode_note->SetFont(wxGetApp().small_font());
        m_stealth_mode_note->SetForegroundColour(UIColors::SecondaryText());
        m_stealth_mode_note->Hide(); // Hidden by default, shown when stealth mode enabled
        auto *note_sizer = new wxBoxSizer(wxHORIZONTAL);
        note_sizer->AddStretchSpacer(1);
        note_sizer->Add(m_stealth_mode_note, 0, wxALIGN_CENTER_VERTICAL);
        note_sizer->AddStretchSpacer(1);
        marlin_sizer->Add(note_sizer, 0, wxEXPAND | wxALL, em / 4);
    }

    // Maximum feedrates group
    if (has_any_visible_setting(
            {"machine_max_feedrate_x", "machine_max_feedrate_y", "machine_max_feedrate_z", "machine_max_feedrate_e"}))
    {
        auto *feedrate_group = CreateFlatStaticBoxSizer(m_marlin_limits_panel, _L("Maximum feedrates"));
        CreateSettingRow(m_marlin_limits_panel, feedrate_group, "machine_max_feedrate_x", _L("X"));
        CreateSettingRow(m_marlin_limits_panel, feedrate_group, "machine_max_feedrate_y", _L("Y"));
        CreateSettingRow(m_marlin_limits_panel, feedrate_group, "machine_max_feedrate_z", _L("Z"));
        CreateSettingRow(m_marlin_limits_panel, feedrate_group, "machine_max_feedrate_e", _L("E"));
        marlin_sizer->Add(feedrate_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Maximum accelerations group
    if (has_any_visible_setting({"machine_max_acceleration_x", "machine_max_acceleration_y",
                                 "machine_max_acceleration_z", "machine_max_acceleration_e",
                                 "machine_max_acceleration_extruding", "machine_max_acceleration_retracting",
                                 "machine_max_acceleration_travel"}))
    {
        auto *accel_group = CreateFlatStaticBoxSizer(m_marlin_limits_panel, _L("Maximum accelerations"));
        CreateSettingRow(m_marlin_limits_panel, accel_group, "machine_max_acceleration_x", _L("X"));
        CreateSettingRow(m_marlin_limits_panel, accel_group, "machine_max_acceleration_y", _L("Y"));
        CreateSettingRow(m_marlin_limits_panel, accel_group, "machine_max_acceleration_z", _L("Z"));
        CreateSettingRow(m_marlin_limits_panel, accel_group, "machine_max_acceleration_e", _L("E"));
        CreateSettingRow(m_marlin_limits_panel, accel_group, "machine_max_acceleration_extruding", _L("Extruding"));
        CreateSettingRow(m_marlin_limits_panel, accel_group, "machine_max_acceleration_retracting", _L("Retracting"));
        CreateSettingRow(m_marlin_limits_panel, accel_group, "machine_max_acceleration_travel", _L("Travel"));
        marlin_sizer->Add(accel_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Jerk limits group
    if (has_any_visible_setting(
            {"machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z", "machine_max_jerk_e"}))
    {
        auto *jerk_group = CreateFlatStaticBoxSizer(m_marlin_limits_panel, _L("Jerk limits"));
        CreateSettingRow(m_marlin_limits_panel, jerk_group, "machine_max_jerk_x", _L("X"));
        CreateSettingRow(m_marlin_limits_panel, jerk_group, "machine_max_jerk_y", _L("Y"));
        CreateSettingRow(m_marlin_limits_panel, jerk_group, "machine_max_jerk_z", _L("Z"));
        CreateSettingRow(m_marlin_limits_panel, jerk_group, "machine_max_jerk_e", _L("E"));
        marlin_sizer->Add(jerk_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Junction deviation group
    if (has_any_visible_setting({"machine_max_junction_deviation"}))
    {
        auto *junction_group = CreateFlatStaticBoxSizer(m_marlin_limits_panel, _L("Junction deviation"));
        CreateSettingRow(m_marlin_limits_panel, junction_group, "machine_max_junction_deviation",
                         _L("Junction deviation"));
        marlin_sizer->Add(junction_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Minimum feedrates group
    if (has_any_visible_setting({"machine_min_extruding_rate", "machine_min_travel_rate"}))
    {
        auto *min_group = CreateFlatStaticBoxSizer(m_marlin_limits_panel, _L("Minimum feedrates"));
        CreateSettingRow(m_marlin_limits_panel, min_group, "machine_min_extruding_rate", _L("Minimum extruding rate"));
        CreateSettingRow(m_marlin_limits_panel, min_group, "machine_min_travel_rate", _L("Minimum travel rate"));
        marlin_sizer->Add(min_group, 0, wxEXPAND | wxALL, em / 4);
    }

    m_marlin_limits_panel->SetSizer(marlin_sizer);
    sizer->Add(m_marlin_limits_panel, 0, wxEXPAND);

    // ========== RRF-style limits panel ==========
    m_rrf_limits_panel = new wxPanel(content, wxID_ANY);
    auto *rrf_sizer = new wxBoxSizer(wxVERTICAL);

    // Only show RRF content if any RRF settings are visible
    if (has_any_visible_setting(
            {"machine_rrf_m566", "machine_rrf_m201", "machine_rrf_m203", "machine_rrf_m204", "machine_rrf_m207"}))
    {
        // Retrieve from machine button
        auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *desc_text = new wxStaticText(m_rrf_limits_panel, wxID_ANY, _L("Machine limit M-codes:"),
                                           wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        desc_text->SetMinSize(wxSize(1, -1)); // Allow text to shrink
        btn_sizer->Add(desc_text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);

        auto *retrieve_btn = new ScalableButton(m_rrf_limits_panel, wxID_ANY, "refresh", _L("Retrieve from machine"),
                                                wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        retrieve_btn->SetToolTip(_L("Retrieve machine limits from connected printer"));
        retrieve_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnRetrieveFromMachine(); });
        btn_sizer->Add(retrieve_btn, 0, wxALIGN_CENTER_VERTICAL);
        rrf_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, em / 4);

        // RRF M-code fields
        auto *rrf_group = CreateFlatStaticBoxSizer(m_rrf_limits_panel, _L("RepRapFirmware M-codes"));
        CreateSettingRow(m_rrf_limits_panel, rrf_group, "machine_rrf_m566", _L("M566 (Jerk)"), true);
        CreateSettingRow(m_rrf_limits_panel, rrf_group, "machine_rrf_m201", _L("M201 (Acceleration)"), true);
        CreateSettingRow(m_rrf_limits_panel, rrf_group, "machine_rrf_m203", _L("M203 (Max feedrate)"), true);
        CreateSettingRow(m_rrf_limits_panel, rrf_group, "machine_rrf_m204", _L("M204 (Acceleration)"), true);
        CreateSettingRow(m_rrf_limits_panel, rrf_group, "machine_rrf_m207", _L("M207 (Retraction)"), true);
        rrf_sizer->Add(rrf_group, 0, wxEXPAND | wxALL, em / 4);
    }

    m_rrf_limits_panel->SetSizer(rrf_sizer);
    sizer->Add(m_rrf_limits_panel, 0, wxEXPAND);

    // Time estimation group (common to both)
    if (has_any_visible_setting({"machine_time_compensation"}))
    {
        auto *time_group = CreateFlatStaticBoxSizer(content, _L("Time estimation"));
        CreateSettingRow(content, time_group, "machine_time_compensation", _L("Time compensation"));
        sizer->Add(time_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);

    // Set initial visibility based on current gcode_flavor
    UpdateMachineLimitsVisibility();

    return content;
}

wxPanel *PrinterSettingsPanel::BuildSingleExtruderMMContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Single extruder multimaterial parameters group
    if (has_any_visible_setting({"cooling_tube_retraction", "cooling_tube_length", "parking_pos_retraction",
                                 "extra_loading_move", "multimaterial_purging", "high_current_on_filament_swap"}))
    {
        auto *semm_group = CreateFlatStaticBoxSizer(content, _L("Single extruder multimaterial parameters"));

        CreateSettingRow(content, semm_group, "cooling_tube_retraction", _L("Cooling tube position"));
        CreateSettingRow(content, semm_group, "cooling_tube_length", _L("Cooling tube length"));
        CreateSettingRow(content, semm_group, "parking_pos_retraction", _L("Filament parking position"));
        CreateSettingRow(content, semm_group, "extra_loading_move", _L("Extra loading distance"));
        CreateSettingRow(content, semm_group, "multimaterial_purging", _L("Purging volume"));
        CreateSettingRow(content, semm_group, "high_current_on_filament_swap",
                         _L("High extruder current on filament swap"));

        sizer->Add(semm_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    return content;
}

bool PrinterSettingsPanel::ShouldShowSingleExtruderMM() const
{
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    bool semm_enabled = config.opt_bool("single_extruder_multi_material");
    return m_extruders_count > 1 && semm_enabled;
}

void PrinterSettingsPanel::UpdateMachineLimitsVisibility()
{
    if (!m_marlin_limits_panel || !m_rrf_limits_panel)
        return;

    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    auto flavor = static_cast<GCodeFlavor>(config.option("gcode_flavor")->getInt());
    bool is_rrf = (flavor == gcfRepRapFirmware || flavor == gcfRapid);

    m_marlin_limits_panel->Show(!is_rrf);
    m_rrf_limits_panel->Show(is_rrf);

    // Show stealth mode note when stealth mode is enabled (Marlin only)
    if (m_stealth_mode_note)
    {
        bool is_marlin = (flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware);
        bool stealth_enabled = is_marlin && config.opt_bool("silent_mode");
        m_stealth_mode_note->Show(stealth_enabled);
    }

    // Re-layout
    Layout();
    FitInside();
}

void PrinterSettingsPanel::OnRetrieveFromMachine()
{
    // Check for physical printer selection
    if (!wxGetApp().preset_bundle->physical_printers.has_selection())
    {
        wxMessageBox(_L("No physical printer selected.\nPlease configure a physical printer with print host first."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    DynamicPrintConfig *pp_config = wxGetApp().preset_bundle->physical_printers.get_selected_printer_config();
    if (!pp_config)
    {
        wxMessageBox(_L("Could not get physical printer configuration."), _L("Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    std::string host = pp_config->opt_string("print_host");
    if (host.empty())
    {
        wxMessageBox(_L("No print host configured.\nPlease configure the print host in the physical printer settings."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    // Create print host and retrieve limits
    std::unique_ptr<PrintHost> print_host(PrintHost::get_print_host(pp_config));
    if (!print_host)
    {
        wxMessageBox(_L("Could not create connection to print host."), _L("Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    wxBusyCursor wait;
    wxString msg;
    PrintHost::MachineLimitsResult limits;

    if (print_host->get_machine_limits(msg, limits))
    {
        DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        bool any_updated = false;

        if (!limits.m566.empty())
        {
            config.set_key_value("machine_rrf_m566", new ConfigOptionString(limits.m566));
            any_updated = true;
        }
        if (!limits.m201.empty())
        {
            config.set_key_value("machine_rrf_m201", new ConfigOptionString(limits.m201));
            any_updated = true;
        }
        if (!limits.m203.empty())
        {
            config.set_key_value("machine_rrf_m203", new ConfigOptionString(limits.m203));
            any_updated = true;
        }
        if (!limits.m204.empty())
        {
            config.set_key_value("machine_rrf_m204", new ConfigOptionString(limits.m204));
            any_updated = true;
        }
        if (!limits.m207.empty())
        {
            config.set_key_value("machine_rrf_m207", new ConfigOptionString(limits.m207));
            any_updated = true;
        }

        if (any_updated)
        {
            // Refresh UI
            RefreshFromConfig();

            // Sync with Printer Settings tab
            if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
            {
                tab->reload_config();
                tab->update_dirty();
                tab->update_changed_ui();
            }

            // Build success message
            wxString success_msg = _L("Machine limits retrieved successfully:\n\n");
            if (!limits.m566.empty())
                success_msg += wxString::FromUTF8(limits.m566) + "\n";
            if (!limits.m201.empty())
                success_msg += wxString::FromUTF8(limits.m201) + "\n";
            if (!limits.m203.empty())
                success_msg += wxString::FromUTF8(limits.m203) + "\n";
            if (!limits.m204.empty())
                success_msg += wxString::FromUTF8(limits.m204) + "\n";
            if (!limits.m207.empty())
                success_msg += wxString::FromUTF8(limits.m207) + "\n";

            wxMessageBox(success_msg, _L("Machine Limits Retrieved"), wxOK | wxICON_INFORMATION, this);
        }
        else
        {
            wxMessageBox(_L("No machine limits were returned by the printer."), _L("Warning"), wxOK | wxICON_WARNING,
                         this);
        }
    }
    else
    {
        wxMessageBox(msg.IsEmpty() ? _L("Failed to retrieve machine limits.") : msg, _L("Error"), wxOK | wxICON_ERROR,
                     this);
    }
}

void PrinterSettingsPanel::CreateSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                            const wxString &label, bool full_width)
{
    // Check sidebar visibility - skip if user has hidden this setting
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        return;

    int em = wxGetApp().em_unit();

    // Create the common row header (icons, label, sizers)
    RowUIContext ctx = CreateRowUIBase(parent, opt_key, label);
    if (!ctx.row_sizer)
        return; // Option not found

    const ConfigOptionDef *opt_def = ctx.opt_def;
    wxStaticBitmap *lock_icon = ctx.lock_icon;
    wxStaticBitmap *undo_icon = ctx.undo_icon;
    wxBoxSizer *row_sizer = ctx.row_sizer;
    wxString tooltip = ctx.tooltip;

    wxWindow *value_ctrl = nullptr;
    const DynamicPrintConfig &config = GetEditedConfig();
    std::string original_value;

    switch (opt_def->type)
    {
    case coBool:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *checkbox = new ::CheckBox(parent);
        checkbox->SetBackgroundColour(SidebarColors::Background());
        if (config.has(opt_key))
        {
            checkbox->SetValue(config.opt_bool(opt_key));
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            checkbox->SetToolTip(tooltip);

        checkbox->Bind(wxEVT_CHECKBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
        value_sizer->AddStretchSpacer(1);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = checkbox;
        break;
    }

    case coEnum:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Use custom ComboBox widget for proper dark mode support
        // DD_NO_CHECK_ICON removes checkmarks, 16em width matches main tabs
        auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16 * em, -1), 0,
                                     nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
        if (opt_def->enum_def && opt_def->enum_def->has_labels())
        {
            for (const std::string &enum_label : opt_def->enum_def->labels())
            {
                combo->Append(from_u8(enum_label));
            }
        }

        if (config.has(opt_key) && opt_def->enum_def && opt_def->enum_def->has_values())
        {
            original_value = config.opt_serialize(opt_key);
            const auto &values = opt_def->enum_def->values();
            for (size_t idx = 0; idx < values.size(); ++idx)
            {
                if (values[idx] == original_value)
                {
                    combo->SetSelection(static_cast<int>(idx));
                    break;
                }
            }
        }
        if (!tooltip.empty())
            combo->SetToolTip(tooltip);

        combo->Bind(wxEVT_COMBOBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(combo, 0, wxALIGN_CENTER_VERTICAL); // Fixed 16em width (matches main tabs)
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = combo;
        break;
    }

    case coInt:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        int min_val = opt_def->min > INT_MIN ? static_cast<int>(opt_def->min) : 0;
        int max_val = opt_def->max < INT_MAX ? static_cast<int>(opt_def->max) : 10000;
        int value = 0;
        if (config.has(opt_key))
        {
            value = config.opt_int(opt_key);
            original_value = config.opt_serialize(opt_key);
        }

        wxString text_value = wxString::Format("%d", value);

        // Simple creation - 70px width like Tab.cpp, left aligned
        auto *spin = new SpinInput(parent, text_value, "", wxDefaultPosition, wxSize(GetScaledInputWidth(), -1), 0,
                                   min_val, max_val, value);

        if (opt_def->step > 1)
            spin->SetStep(static_cast<int>(opt_def->step));

        if (!tooltip.empty())
            spin->SetToolTip(tooltip);

        spin->Bind(wxEVT_SPINCTRL, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = spin;
        break;
    }

    case coFloat:
    case coFloatOrPercent:
    case coPercent:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition,
                                     wxSize(GetScaledInputWidth(), -1));
        wxGetApp().UpdateDarkUI(text);

        if (config.has(opt_key))
        {
            wxString value_str = from_u8(config.opt_serialize(opt_key));
            text->SetValue(value_str);
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL);

        // Add sidetext (units) if available - strip parenthetical notes for compact display
        if (!opt_def->sidetext.empty())
        {
            std::string sidetext = opt_def->sidetext;
            size_t paren_pos = sidetext.find('(');
            if (paren_pos != std::string::npos)
                sidetext = sidetext.substr(0, paren_pos);
            boost::trim(sidetext);
            if (!sidetext.empty())
            {
                auto *unit_text = new wxStaticText(parent, wxID_ANY, from_u8(sidetext));
                value_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            }
        }

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }

    case coFloats:
    {
        // Handle vector float options (like machine_max_feedrate_x which are ConfigOptionFloats)
        // Only show the first value (normal mode), not silent mode value
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition,
                                     wxSize(GetScaledInputWidth(), -1));
        wxGetApp().UpdateDarkUI(text);

        if (config.has(opt_key))
        {
            auto *opt = config.option<ConfigOptionFloats>(opt_key);
            if (opt && !opt->values.empty())
            {
                // Show only the first value (normal mode)
                text->SetValue(wxString::Format("%g", opt->values[0]));
            }
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL); // Fixed 60px width

        // Add sidetext (units) if available - strip parenthetical notes for compact display
        if (!opt_def->sidetext.empty())
        {
            std::string sidetext = opt_def->sidetext;
            size_t paren_pos = sidetext.find('(');
            if (paren_pos != std::string::npos)
                sidetext = sidetext.substr(0, paren_pos);
            boost::trim(sidetext);
            if (!sidetext.empty())
            {
                auto *unit_text = new wxStaticText(parent, wxID_ANY, from_u8(sidetext));
                value_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            }
        }

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }

    case coString:
    case coStrings:
    default:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, wxDefaultSize);
        wxGetApp().UpdateDarkUI(text);
        text->SetMinSize(wxSize(1, -1)); // Allow text to shrink

        if (config.has(opt_key))
        {
            wxString value_str = from_u8(config.opt_serialize(opt_key));
            text->SetValue(value_str);
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 1, wxEXPAND); // Always shrink with sizer
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }
    }

    if (value_ctrl)
    {
        SettingUIElements ui_elem;
        ui_elem.control = value_ctrl;
        ui_elem.lock_icon = lock_icon;
        ui_elem.undo_icon = undo_icon;
        ui_elem.original_value = original_value;
        m_setting_controls[opt_key] = ui_elem;

        // Set initial icon state
        UpdateUndoUI(opt_key);

        // Bind undo icon click to revert value
        undo_icon->Bind(wxEVT_LEFT_DOWN,
                        [this, opt_key](wxMouseEvent &)
                        {
                            auto it = m_setting_controls.find(opt_key);
                            if (it == m_setting_controls.end())
                                return;

                            const ConfigOptionDef *def = print_config_def.get(opt_key);
                            if (!def)
                                return;

                            // Revert to original value
                            switch (def->type)
                            {
                            case coBool:
                                if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
                                {
                                    cb->SetValue(it->second.original_value == "1");
                                }
                                break;
                            case coInt:
                                if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
                                {
                                    spin->SetValue(std::stoi(it->second.original_value));
                                }
                                break;
                            case coEnum:
                                if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
                                {
                                    if (def->enum_def && def->enum_def->has_values())
                                    {
                                        const auto &values = def->enum_def->values();
                                        for (size_t idx = 0; idx < values.size(); ++idx)
                                        {
                                            if (values[idx] == it->second.original_value)
                                            {
                                                combo->SetSelection(static_cast<int>(idx));
                                                break;
                                            }
                                        }
                                    }
                                }
                                break;
                            default:
                                if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
                                {
                                    text_input->SetValue(from_u8(it->second.original_value));
                                }
                                else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
                                {
                                    text->SetValue(from_u8(it->second.original_value));
                                }
                                break;
                            }

                            OnSettingChanged(opt_key);
                            UpdateUndoUI(opt_key);
                        });
    }

    sizer->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
}

void PrinterSettingsPanel::CreateMultilineSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                                     const wxString &label, int num_lines)
{
    // Check sidebar visibility - skip if user has hidden this setting
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        return;

    int em = wxGetApp().em_unit();

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : from_u8(opt_def->tooltip);

    // Vertical layout: label on top, text control below
    auto *container_sizer = new wxBoxSizer(wxVERTICAL);

    // Header row with icons and label
    auto *header_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    auto *lock_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("lock_closed"));
    lock_icon->SetMinSize(GetScaledIconSizeWx());
    lock_icon->SetBackgroundColour(bg_color);
    lock_icon->SetToolTip(_L("Value is same as in the system preset"));
    header_sizer->Add(lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    auto *undo_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("dot"));
    undo_icon->SetMinSize(GetScaledIconSizeWx());
    undo_icon->SetBackgroundColour(bg_color);
    header_sizer->Add(undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    wxString label_with_colon = label + ":";
    auto *label_text = new wxStaticText(parent, wxID_ANY, label_with_colon);
    label_text->SetBackgroundColour(bg_color);
    if (!tooltip.empty())
        label_text->SetToolTip(tooltip);
    header_sizer->Add(label_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

    container_sizer->Add(header_sizer, 0, wxEXPAND);

    // Multi-line text control - full width
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    std::string original_value;

    int text_height = num_lines * em * 1.5;
    auto *text = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, text_height),
                                wxTE_MULTILINE | wxHSCROLL | wxBORDER_SIMPLE);

    if (config.has(opt_key))
    {
        wxString value_str = from_u8(config.opt_serialize(opt_key));
        text->SetValue(value_str);
        original_value = config.opt_serialize(opt_key);
    }
    if (!tooltip.empty())
        text->SetToolTip(tooltip);

    text->Bind(wxEVT_KILL_FOCUS,
               [this, opt_key](wxFocusEvent &evt)
               {
                   OnSettingChanged(opt_key);
                   evt.Skip();
               });

    container_sizer->Add(text, 0, wxEXPAND | wxTOP, em / 4);

    // Store UI elements
    SettingUIElements ui_elem;
    ui_elem.control = text;
    ui_elem.lock_icon = lock_icon;
    ui_elem.undo_icon = undo_icon;
    ui_elem.label_text = label_text;
    ui_elem.original_value = original_value;
    m_setting_controls[opt_key] = ui_elem;

    UpdateUndoUI(opt_key);

    // Bind undo icon click
    undo_icon->Bind(wxEVT_LEFT_DOWN,
                    [this, opt_key](wxMouseEvent &)
                    {
                        auto it = m_setting_controls.find(opt_key);
                        if (it == m_setting_controls.end())
                            return;

                        if (auto *txt = dynamic_cast<wxTextCtrl *>(it->second.control))
                        {
                            txt->SetValue(from_u8(it->second.original_value));
                        }

                        OnSettingChanged(opt_key);
                        UpdateUndoUI(opt_key);
                    });

    sizer->Add(container_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
}

void PrinterSettingsPanel::CreateExtruderSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                                    const wxString &label, size_t extruder_idx)
{
    // Check sidebar visibility using extruder-specific key - skip if user has hidden this setting
    std::string visibility_key = opt_key + "#" + std::to_string(extruder_idx);
    if (get_app_config()->get("sidebar_visibility", visibility_key) == "0")
        return;

    int em = wxGetApp().em_unit();

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : from_u8(opt_def->tooltip);

    // Left side sizer: icons + label
    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    auto *lock_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("lock_closed"));
    lock_icon->SetMinSize(GetScaledIconSizeWx());
    lock_icon->SetBackgroundColour(bg_color);
    lock_icon->SetToolTip(_L("Value is same as in the system preset"));
    left_sizer->Add(lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    auto *undo_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("dot"));
    undo_icon->SetMinSize(GetScaledIconSizeWx());
    undo_icon->SetBackgroundColour(bg_color);
    left_sizer->Add(undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    wxString label_with_colon = label + ":";
    auto *label_text = new wxStaticText(parent, wxID_ANY, label_with_colon, wxDefaultPosition, wxDefaultSize,
                                        wxST_ELLIPSIZE_END);
    label_text->SetMinSize(wxSize(1, -1));
    label_text->SetBackgroundColour(bg_color);
    if (!tooltip.empty())
        label_text->SetToolTip(tooltip);
    left_sizer->Add(label_text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

    row_sizer->Add(left_sizer, 1, wxEXPAND);

    wxWindow *value_ctrl = nullptr;
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    // Get SAVED preset config for original_value (not the edited/in-memory version)
    const Preset &saved_preset = wxGetApp().preset_bundle->printers.get_selected_preset();
    const DynamicPrintConfig &saved_config = saved_preset.config;
    std::string original_value;

    // Create composite key for tracking this specific extruder's setting
    std::string composite_key = opt_key + "#" + std::to_string(extruder_idx);

    switch (opt_def->type)
    {
    case coBools:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *checkbox = new ::CheckBox(parent);
        checkbox->SetBackgroundColour(SidebarColors::Background());
        if (config.has(opt_key))
        {
            auto *opt = config.option<ConfigOptionBools>(opt_key);
            if (opt && extruder_idx < opt->values.size())
            {
                checkbox->SetValue(opt->values[extruder_idx]);
            }
        }
        // Get original value from SAVED preset (for undo comparison)
        if (saved_config.has(opt_key))
        {
            auto *saved_opt = saved_config.option<ConfigOptionBools>(opt_key);
            if (saved_opt && extruder_idx < saved_opt->values.size())
                original_value = saved_opt->values[extruder_idx] ? "1" : "0";
        }
        if (!tooltip.empty())
            checkbox->SetToolTip(tooltip);

        checkbox->Bind(wxEVT_CHECKBOX, [this, opt_key, extruder_idx](wxCommandEvent &)
                       { OnExtruderSettingChanged(opt_key, extruder_idx); });

        value_sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
        value_sizer->AddStretchSpacer(1);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = checkbox;
        break;
    }

    case coFloats:
    case coPercents:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition,
                                     wxSize(GetScaledInputWidth(), -1));
        wxGetApp().UpdateDarkUI(text);

        if (config.has(opt_key))
        {
            auto *opt = config.option<ConfigOptionFloats>(opt_key);
            if (opt && extruder_idx < opt->values.size())
            {
                text->SetValue(wxString::Format("%g", opt->values[extruder_idx]));
            }
        }
        // Get original value from SAVED preset (for undo comparison)
        if (saved_config.has(opt_key))
        {
            auto *saved_opt = saved_config.option<ConfigOptionFloats>(opt_key);
            if (saved_opt && extruder_idx < saved_opt->values.size())
                original_value = into_u8(wxString::Format("%g", saved_opt->values[extruder_idx]));
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key, extruder_idx](wxFocusEvent &evt)
                   {
                       OnExtruderSettingChanged(opt_key, extruder_idx);
                       evt.Skip();
                   });

        value_sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL); // Fixed 60px width

        if (!opt_def->sidetext.empty())
        {
            std::string sidetext = opt_def->sidetext;
            size_t paren_pos = sidetext.find('(');
            if (paren_pos != std::string::npos)
                sidetext = sidetext.substr(0, paren_pos);
            boost::trim(sidetext);
            if (!sidetext.empty())
            {
                auto *unit_text = new wxStaticText(parent, wxID_ANY, from_u8(sidetext));
                value_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            }
        }

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }

    case coInts:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        int min_val = opt_def->min > INT_MIN ? static_cast<int>(opt_def->min) : 0;
        int max_val = opt_def->max < INT_MAX ? static_cast<int>(opt_def->max) : 10000;
        int value = 0;
        if (config.has(opt_key))
        {
            auto *opt = config.option<ConfigOptionInts>(opt_key);
            if (opt && extruder_idx < opt->values.size())
            {
                value = opt->values[extruder_idx];
            }
        }
        // Get original value from SAVED preset (for undo comparison)
        if (saved_config.has(opt_key))
        {
            auto *saved_opt = saved_config.option<ConfigOptionInts>(opt_key);
            if (saved_opt && extruder_idx < saved_opt->values.size())
                original_value = std::to_string(saved_opt->values[extruder_idx]);
        }

        wxString text_value = wxString::Format("%d", value);

        // Simple creation - 70px width like Tab.cpp, left aligned
        auto *spin = new SpinInput(parent, text_value, "", wxDefaultPosition, wxSize(GetScaledInputWidth(), -1), 0,
                                   min_val, max_val, value);

        if (opt_def->step > 1)
            spin->SetStep(static_cast<int>(opt_def->step));

        if (!tooltip.empty())
            spin->SetToolTip(tooltip);

        spin->Bind(wxEVT_SPINCTRL, [this, opt_key, extruder_idx](wxCommandEvent &)
                   { OnExtruderSettingChanged(opt_key, extruder_idx); });

        value_sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = spin;
        break;
    }

    case coStrings:
    {
        // Special handling for extruder_colour with color picker
        if (opt_key == "extruder_colour")
        {
            auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);

            // Get current color
            wxColour current_color = *wxWHITE;
            if (config.has(opt_key))
            {
                auto *opt = config.option<ConfigOptionStrings>(opt_key);
                if (opt && extruder_idx < opt->values.size() && !opt->values[extruder_idx].empty())
                {
                    current_color = wxColour(from_u8(opt->values[extruder_idx]));
                }
            }
            // Get original value from SAVED preset (for undo comparison)
            if (saved_config.has(opt_key))
            {
                auto *saved_opt = saved_config.option<ConfigOptionStrings>(opt_key);
                if (saved_opt && extruder_idx < saved_opt->values.size())
                    original_value = saved_opt->values[extruder_idx];
            }

            auto *color_btn = new wxButton(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(em * 4, -1));
            color_btn->SetBackgroundColour(current_color);
            color_btn->SetMinSize(wxSize(1, -1));

            color_btn->Bind(wxEVT_BUTTON,
                            [this, color_btn, opt_key, extruder_idx](wxCommandEvent &)
                            {
                                wxColourData data;
                                data.SetColour(color_btn->GetBackgroundColour());
                                wxColourDialog dlg(this, &data);
                                if (dlg.ShowModal() == wxID_OK)
                                {
                                    wxColour new_color = dlg.GetColourData().GetColour();
                                    color_btn->SetBackgroundColour(new_color);
                                    color_btn->Refresh();
                                    OnExtruderSettingChanged(opt_key, extruder_idx);
                                }
                            });

            value_sizer->Add(color_btn, 1, wxEXPAND);

            // Add "Reset to Filament Color" button
            auto *reset_btn = new ScalableButton(parent, wxID_ANY, "undo", _L("Reset"), wxDefaultSize,
                                                 wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
            reset_btn->SetToolTip(_L("Reset to Filament Color"));
            reset_btn->Bind(wxEVT_BUTTON,
                            [this, color_btn, opt_key, extruder_idx](wxCommandEvent &)
                            {
                                DynamicPrintConfig &cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
                                auto *opt = cfg.option<ConfigOptionStrings>(opt_key, true);
                                if (opt && extruder_idx < opt->values.size())
                                {
                                    opt->values[extruder_idx] = "";
                                    color_btn->SetBackgroundColour(*wxWHITE);
                                    color_btn->Refresh();

                                    wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);
                                    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                                    {
                                        tab->reload_config();
                                        tab->update_dirty();
                                        tab->update_changed_ui();
                                    }
                                    if (GetPlater())
                                        GetPlater()->on_config_change(cfg);
                                }
                            });
            value_sizer->Add(reset_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

            row_sizer->Add(value_sizer, 1, wxEXPAND);
            value_ctrl = color_btn;
        }
        else
        {
            auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, wxDefaultSize);
            wxGetApp().UpdateDarkUI(text);
            text->SetMinSize(wxSize(1, -1));

            if (config.has(opt_key))
            {
                auto *opt = config.option<ConfigOptionStrings>(opt_key);
                if (opt && extruder_idx < opt->values.size())
                {
                    text->SetValue(from_u8(opt->values[extruder_idx]));
                }
            }
            // Get original value from SAVED preset (for undo comparison)
            if (saved_config.has(opt_key))
            {
                auto *saved_opt = saved_config.option<ConfigOptionStrings>(opt_key);
                if (saved_opt && extruder_idx < saved_opt->values.size())
                    original_value = saved_opt->values[extruder_idx];
            }
            if (!tooltip.empty())
                text->SetToolTip(tooltip);

            text->Bind(wxEVT_KILL_FOCUS,
                       [this, opt_key, extruder_idx](wxFocusEvent &evt)
                       {
                           OnExtruderSettingChanged(opt_key, extruder_idx);
                           evt.Skip();
                       });

            value_sizer->Add(text, 1, wxEXPAND);
            row_sizer->Add(value_sizer, 1, wxEXPAND);
            value_ctrl = text;
        }
        break;
    }

    case coPoints:
    {
        // Special handling for extruder_offset (Vec2d)
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);

        double x_val = 0, y_val = 0;
        if (config.has(opt_key))
        {
            auto *opt = config.option<ConfigOptionPoints>(opt_key);
            if (opt && extruder_idx < opt->values.size())
            {
                x_val = opt->values[extruder_idx].x();
                y_val = opt->values[extruder_idx].y();
            }
        }
        // Get original value from SAVED preset (for undo comparison)
        if (saved_config.has(opt_key))
        {
            auto *saved_opt = saved_config.option<ConfigOptionPoints>(opt_key);
            if (saved_opt && extruder_idx < saved_opt->values.size())
            {
                double saved_x = saved_opt->values[extruder_idx].x();
                double saved_y = saved_opt->values[extruder_idx].y();
                original_value = std::to_string(saved_x) + "x" + std::to_string(saved_y);
            }
        }

        auto *x_text = new wxTextCtrl(parent, wxID_ANY, wxString::Format("%g", x_val), wxDefaultPosition,
                                      wxSize(GetScaledSmallInputWidth(), -1), wxBORDER_SIMPLE);
        auto *y_text = new wxTextCtrl(parent, wxID_ANY, wxString::Format("%g", y_val), wxDefaultPosition,
                                      wxSize(GetScaledSmallInputWidth(), -1), wxBORDER_SIMPLE);

        // Apply theme colors on creation - use unified accessors
        {
#ifdef _WIN32
            // Disable visual styles so SetBackgroundColour works properly
            SetWindowTheme(x_text->GetHWND(), L"", L"");
            SetWindowTheme(y_text->GetHWND(), L"", L"");
#endif
            wxColour bg = SidebarColors::InputBackground();
            wxColour fg = SidebarColors::InputForeground();
            x_text->SetBackgroundColour(bg);
            y_text->SetBackgroundColour(bg);
            x_text->SetForegroundColour(fg);
            y_text->SetForegroundColour(fg);
#ifdef _WIN32
            RedrawWindow(x_text->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
            RedrawWindow(y_text->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
#endif
        }

        auto update_point = [this, opt_key, extruder_idx, x_text, y_text]()
        {
            double x = 0, y = 0;
            x_text->GetValue().ToDouble(&x);
            y_text->GetValue().ToDouble(&y);

            DynamicPrintConfig &cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
            auto *opt = cfg.option<ConfigOptionPoints>(opt_key, true);
            if (opt)
            {
                while (opt->values.size() <= extruder_idx)
                    opt->values.push_back(Vec2d(0, 0));
                opt->values[extruder_idx] = Vec2d(x, y);

                wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);
                if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                {
                    tab->reload_config();
                    tab->update_dirty();
                    tab->update_changed_ui();
                }
                if (GetPlater())
                    GetPlater()->on_config_change(cfg);
            }
        };

        x_text->Bind(wxEVT_KILL_FOCUS,
                     [update_point](wxFocusEvent &evt)
                     {
                         update_point();
                         evt.Skip();
                     });
        y_text->Bind(wxEVT_KILL_FOCUS,
                     [update_point](wxFocusEvent &evt)
                     {
                         update_point();
                         evt.Skip();
                     });

        value_sizer->Add(new wxStaticText(parent, wxID_ANY, "X:"), 0, wxALIGN_CENTER_VERTICAL);
        value_sizer->Add(x_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());
        value_sizer->Add(new wxStaticText(parent, wxID_ANY, " Y:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 2);
        value_sizer->Add(y_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = x_text; // Store first control for tracking
        break;
    }

    case coEnums:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Use custom ComboBox widget for proper dark mode support
        // DD_NO_CHECK_ICON removes checkmarks, 16em width matches main tabs
        auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16 * em, -1), 0,
                                     nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
        if (opt_def->enum_def && opt_def->enum_def->has_labels())
        {
            for (const std::string &enum_label : opt_def->enum_def->labels())
            {
                combo->Append(from_u8(enum_label));
            }
        }

        if (config.has(opt_key) && opt_def->enum_def && opt_def->enum_def->has_values())
        {
            // For vector enums, use vserialize() to get string values
            const ConfigOption *opt = config.option(opt_key);
            if (opt)
            {
                auto *vec_opt = dynamic_cast<const ConfigOptionVectorBase *>(opt);
                if (vec_opt && extruder_idx < vec_opt->size())
                {
                    std::vector<std::string> serialized = vec_opt->vserialize();
                    if (extruder_idx < serialized.size())
                    {
                        std::string current_value = serialized[extruder_idx];
                        // Find matching value in enum values list
                        const auto &values = opt_def->enum_def->values();
                        for (size_t idx = 0; idx < values.size(); ++idx)
                        {
                            if (values[idx] == current_value)
                            {
                                combo->SetSelection(static_cast<int>(idx));
                                break;
                            }
                        }
                    }
                }
            }
        }
        // Get original value from SAVED preset (for undo comparison)
        if (saved_config.has(opt_key) && opt_def->enum_def && opt_def->enum_def->has_values())
        {
            const ConfigOption *saved_opt = saved_config.option(opt_key);
            if (saved_opt)
            {
                auto *saved_vec = dynamic_cast<const ConfigOptionVectorBase *>(saved_opt);
                if (saved_vec && extruder_idx < saved_vec->size())
                {
                    std::vector<std::string> saved_serialized = saved_vec->vserialize();
                    if (extruder_idx < saved_serialized.size())
                        original_value = saved_serialized[extruder_idx];
                }
            }
        }
        if (!tooltip.empty())
            combo->SetToolTip(tooltip);

        combo->Bind(wxEVT_COMBOBOX, [this, opt_key, extruder_idx](wxCommandEvent &)
                    { OnExtruderSettingChanged(opt_key, extruder_idx); });

        value_sizer->Add(combo, 0, wxALIGN_CENTER_VERTICAL); // Fixed 16em width (matches main tabs)
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = combo;
        break;
    }

    default:
        break;
    }

    if (value_ctrl)
    {
        SettingUIElements ui_elem;
        ui_elem.control = value_ctrl;
        ui_elem.lock_icon = lock_icon;
        ui_elem.undo_icon = undo_icon;
        ui_elem.label_text = label_text;
        ui_elem.original_value = original_value;
        m_setting_controls[composite_key] = ui_elem;

        // Initial icon state - show dot for now
        undo_icon->SetBitmap(*get_bmp_bundle("dot"));

        // Bind undo icon click to revert extruder setting
        undo_icon->Bind(wxEVT_LEFT_DOWN,
                        [this, opt_key, extruder_idx, composite_key](wxMouseEvent &)
                        {
                            auto it = m_setting_controls.find(composite_key);
                            if (it == m_setting_controls.end())
                                return;

                            const ConfigOptionDef *def = print_config_def.get(opt_key);
                            if (!def)
                                return;

                            const std::string &original = it->second.original_value;

                            // Restore control value based on type
                            switch (def->type)
                            {
                            case coBools:
                                if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
                                {
                                    cb->SetValue(original == "1");
                                }
                                break;
                            case coFloats:
                            case coPercents:
                                if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
                                {
                                    text_input->SetValue(from_u8(original));
                                }
                                break;
                            case coInts:
                                if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
                                {
                                    try
                                    {
                                        spin->SetValue(std::stoi(original));
                                    }
                                    catch (...)
                                    {
                                    }
                                }
                                break;
                            case coEnums:
                                if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
                                {
                                    if (def->enum_def && def->enum_def->has_values())
                                    {
                                        const auto &values = def->enum_def->values();
                                        for (size_t idx = 0; idx < values.size(); ++idx)
                                        {
                                            if (values[idx] == original)
                                            {
                                                combo->SetSelection(static_cast<int>(idx));
                                                break;
                                            }
                                        }
                                    }
                                }
                                break;
                            case coStrings:
                                if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
                                {
                                    text_input->SetValue(from_u8(original));
                                }
                                else if (auto *btn = dynamic_cast<wxButton *>(it->second.control))
                                {
                                    // Color button - restore color
                                    if (!original.empty())
                                        btn->SetBackgroundColour(wxColour(from_u8(original)));
                                    else
                                        btn->SetBackgroundColour(*wxWHITE);
                                    btn->Refresh();
                                }
                                break;
                            default:
                                break;
                            }

                            OnExtruderSettingChanged(opt_key, extruder_idx);
                        });
    }

    sizer->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
}

void PrinterSettingsPanel::OnExtruderSettingChanged(const std::string &opt_key, size_t extruder_idx)
{
    if (m_disable_update)
        return;

    m_disable_update = true;

    std::string composite_key = opt_key + "#" + std::to_string(extruder_idx);
    auto it = m_setting_controls.find(composite_key);
    if (it == m_setting_controls.end())
    {
        m_disable_update = false;
        return;
    }

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
    {
        m_disable_update = false;
        return;
    }

    DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;

    switch (opt_def->type)
    {
    case coBools:
        if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
        {
            auto *opt = config.option<ConfigOptionBools>(opt_key, true);
            if (opt)
            {
                while (opt->values.size() <= extruder_idx)
                    opt->values.push_back(false);
                opt->values[extruder_idx] = cb->GetValue();
            }
        }
        break;

    case coFloats:
    case coPercents:
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            double new_value = 0;
            if (text_input->GetValue().ToDouble(&new_value))
            {
                auto *opt = config.option<ConfigOptionFloats>(opt_key, true);
                if (opt)
                {
                    while (opt->values.size() <= extruder_idx)
                        opt->values.push_back(0.0);
                    opt->values[extruder_idx] = new_value;
                }
            }
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            double new_value = 0;
            if (text->GetValue().ToDouble(&new_value))
            {
                auto *opt = config.option<ConfigOptionFloats>(opt_key, true);
                if (opt)
                {
                    while (opt->values.size() <= extruder_idx)
                        opt->values.push_back(0.0);
                    opt->values[extruder_idx] = new_value;
                }
            }
        }
        break;

    case coInts:
        if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
        {
            auto *opt = config.option<ConfigOptionInts>(opt_key, true);
            if (opt)
            {
                while (opt->values.size() <= extruder_idx)
                    opt->values.push_back(0);
                opt->values[extruder_idx] = spin->GetValue();
            }
        }
        break;

    case coStrings:
        if (opt_key == "extruder_colour")
        {
            if (auto *btn = dynamic_cast<wxButton *>(it->second.control))
            {
                wxColour color = btn->GetBackgroundColour();
                std::string color_str = into_u8(color.GetAsString(wxC2S_HTML_SYNTAX));
                auto *opt = config.option<ConfigOptionStrings>(opt_key, true);
                if (opt)
                {
                    while (opt->values.size() <= extruder_idx)
                        opt->values.push_back("");
                    opt->values[extruder_idx] = color_str;
                }
            }
        }
        else if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            auto *opt = config.option<ConfigOptionStrings>(opt_key, true);
            if (opt)
            {
                while (opt->values.size() <= extruder_idx)
                    opt->values.push_back("");
                opt->values[extruder_idx] = into_u8(text_input->GetValue());
            }
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            auto *opt = config.option<ConfigOptionStrings>(opt_key, true);
            if (opt)
            {
                while (opt->values.size() <= extruder_idx)
                    opt->values.push_back("");
                opt->values[extruder_idx] = into_u8(text->GetValue());
            }
        }
        break;

    case coEnums:
        if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
        {
            int sel = combo->GetSelection();
            if (sel != wxNOT_FOUND && opt_def->enum_def && opt_def->enum_def->has_values())
            {
                int enum_val = opt_def->enum_def->index_to_enum(sel);
                auto *opt = dynamic_cast<ConfigOptionEnumsGeneric *>(config.optptr(opt_key, true));
                if (opt)
                {
                    while (opt->values.size() <= extruder_idx)
                        opt->values.push_back(0);
                    opt->values[extruder_idx] = enum_val;
                }
            }
        }
        break;

    default:
        break;
    }

    // Mark preset as dirty and sync
    wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);

    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
    {
        // Sidebar and tab share the same config object, so load_config would
        // find no diff. Force the tab to re-read UI fields and update undo state.
        tab->reload_config();
        tab->update_dirty();
        tab->update_changed_ui();
    }

    if (GetPlater())
        GetPlater()->on_config_change(config);

    // Apply toggle logic to enable/disable dependent options
    ApplyToggleLogic();

    // Update undo UI for this setting
    UpdateUndoUI(composite_key);

    // If nozzle_diameter changed, also update the top nozzle spinners
    if (opt_key == "nozzle_diameter")
    {
        wxGetApp().sidebar().refresh_printer_nozzles();
    }

    m_disable_update = false;
}

wxPanel *PrinterSettingsPanel::BuildExtruderContent(size_t extruder_idx)
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Note: Nozzle diameter is permanently shown in sidebar header (top spinners),
    // so it's not included here in the extruder accordion tabs.

    // Preview group
    if (has_extruder_visible_setting({"extruder_colour"}, extruder_idx))
    {
        auto *preview_group = CreateFlatStaticBoxSizer(content, _L("Preview"));
        CreateExtruderSettingRow(content, preview_group, "extruder_colour", _L("Extruder color"), extruder_idx);
        sizer->Add(preview_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // "Apply below settings to other extruders" button (only if multiple extruders AND any applicable settings visible)
    if (m_extruders_count > 1 && has_extruder_visible_setting({"fan_spinup_time",
                                                               "fan_spinup_response_type",
                                                               "min_layer_height",
                                                               "max_layer_height",
                                                               "extruder_offset",
                                                               "retract_length",
                                                               "retract_lift",
                                                               "retract_lift_above",
                                                               "retract_lift_below",
                                                               "retract_speed",
                                                               "deretract_speed",
                                                               "retract_restart_extra",
                                                               "retract_before_travel",
                                                               "retract_layer_change",
                                                               "wipe",
                                                               "wipe_extend",
                                                               "retract_before_wipe",
                                                               "wipe_length",
                                                               "travel_ramping_lift",
                                                               "travel_slope",
                                                               "travel_max_lift",
                                                               "travel_lift_before_obstacle",
                                                               "retract_length_toolchange",
                                                               "retract_restart_extra_toolchange"},
                                                              extruder_idx))
    {
        auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *btn = new ScalableButton(content, wxID_ANY, "copy", _L("Apply below settings to other extruders"),
                                       wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        btn->Bind(wxEVT_BUTTON,
                  [this, extruder_idx](wxCommandEvent &)
                  {
                      static const std::vector<std::string> extruder_options = {"fan_spinup_time",
                                                                                "fan_spinup_response_type",
                                                                                "min_layer_height",
                                                                                "max_layer_height",
                                                                                "extruder_offset",
                                                                                "retract_length",
                                                                                "retract_lift",
                                                                                "retract_lift_above",
                                                                                "retract_lift_below",
                                                                                "retract_speed",
                                                                                "deretract_speed",
                                                                                "retract_restart_extra",
                                                                                "retract_before_travel",
                                                                                "retract_layer_change",
                                                                                "wipe",
                                                                                "wipe_extend",
                                                                                "retract_before_wipe",
                                                                                "wipe_length",
                                                                                "travel_ramping_lift",
                                                                                "travel_slope",
                                                                                "travel_max_lift",
                                                                                "travel_lift_before_obstacle",
                                                                                "retract_length_toolchange",
                                                                                "retract_restart_extra_toolchange"};

                      DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;

                      for (const std::string &opt : extruder_options)
                      {
                          ConfigOption *opt_ptr = config.option(opt, true);
                          if (!opt_ptr)
                              continue;

                          auto *vec_opt = dynamic_cast<ConfigOptionVectorBase *>(opt_ptr);
                          if (!vec_opt)
                              continue;

                          for (size_t ext = 0; ext < m_extruders_count; ++ext)
                          {
                              if (ext == extruder_idx)
                                  continue;
                              vec_opt->set_at(opt_ptr, ext, extruder_idx);
                          }
                      }

                      wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);
                      if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                      {
                          tab->reload_config();
                          tab->update_dirty();
                          tab->update_changed_ui();
                      }
                      if (GetPlater())
                          GetPlater()->on_config_change(config);

                      RefreshFromConfig();
                  });
        btn_sizer->AddStretchSpacer(1);
        btn_sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);
        btn_sizer->AddStretchSpacer(1);
        sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, em / 4);
    }

    // Cooling fan group
    if (has_extruder_visible_setting({"fan_spinup_time", "fan_spinup_response_type"}, extruder_idx))
    {
        auto *fan_group = CreateFlatStaticBoxSizer(content, _L("Cooling fan"));
        CreateExtruderSettingRow(content, fan_group, "fan_spinup_time", _L("Fan spin-up time"), extruder_idx);
        CreateExtruderSettingRow(content, fan_group, "fan_spinup_response_type", _L("Response type"), extruder_idx);
        sizer->Add(fan_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Layer height limits group
    if (has_extruder_visible_setting({"min_layer_height", "max_layer_height"}, extruder_idx))
    {
        auto *height_group = CreateFlatStaticBoxSizer(content, _L("Layer height limits"));
        CreateExtruderSettingRow(content, height_group, "min_layer_height", _L("Minimum"), extruder_idx);
        CreateExtruderSettingRow(content, height_group, "max_layer_height", _L("Maximum"), extruder_idx);
        sizer->Add(height_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Position group (for multi-extruder)
    if (m_extruders_count > 1 && has_extruder_visible_setting({"extruder_offset"}, extruder_idx))
    {
        auto *pos_group = CreateFlatStaticBoxSizer(content, _L("Position (for multi-extruder printers)"));
        CreateExtruderSettingRow(content, pos_group, "extruder_offset", _L("Extruder offset"), extruder_idx);
        sizer->Add(pos_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Travel lift group
    if (has_extruder_visible_setting({"retract_lift", "travel_ramping_lift", "travel_max_lift", "travel_slope",
                                      "travel_lift_before_obstacle", "retract_lift_above", "retract_lift_below"},
                                     extruder_idx))
    {
        auto *lift_group = CreateFlatStaticBoxSizer(content, _L("Travel lift"));
        CreateExtruderSettingRow(content, lift_group, "retract_lift", _L("Lift Z"), extruder_idx);
        CreateExtruderSettingRow(content, lift_group, "travel_ramping_lift", _L("Ramping lift"), extruder_idx);
        CreateExtruderSettingRow(content, lift_group, "travel_max_lift", _L("Max lift"), extruder_idx);
        CreateExtruderSettingRow(content, lift_group, "travel_slope", _L("Travel slope"), extruder_idx);
        CreateExtruderSettingRow(content, lift_group, "travel_lift_before_obstacle", _L("Lift before obstacle"),
                                 extruder_idx);
        CreateExtruderSettingRow(content, lift_group, "retract_lift_above", _L("Only lift above"), extruder_idx);
        CreateExtruderSettingRow(content, lift_group, "retract_lift_below", _L("Only lift below"), extruder_idx);
        sizer->Add(lift_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Retraction / Wipe group
    if (has_extruder_visible_setting({"retract_length", "retract_speed", "deretract_speed", "retract_restart_extra",
                                      "retract_before_wipe", "retract_before_travel", "retract_layer_change", "wipe",
                                      "wipe_extend", "wipe_length"},
                                     extruder_idx))
    {
        auto *retract_group = CreateFlatStaticBoxSizer(content, _L("Retraction / Wipe"));
        CreateExtruderSettingRow(content, retract_group, "retract_length", _L("Retraction length"), extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "retract_speed", _L("Retraction speed"), extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "deretract_speed", _L("Deretraction speed"), extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "retract_restart_extra", _L("Restart extra"), extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "retract_before_wipe", _L("Retract before wipe"),
                                 extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "retract_before_travel", _L("Min travel after retraction"),
                                 extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "retract_layer_change", _L("Retract on layer change"),
                                 extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "wipe", _L("Wipe while retracting"), extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "wipe_extend", _L("Wipe extend"), extruder_idx);
        CreateExtruderSettingRow(content, retract_group, "wipe_length", _L("Wipe length"), extruder_idx);
        sizer->Add(retract_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Tool change retraction group
    if (has_extruder_visible_setting({"retract_length_toolchange", "retract_restart_extra_toolchange"}, extruder_idx))
    {
        auto *toolchange_group = CreateFlatStaticBoxSizer(content, _L("Retraction when tool is disabled"));
        CreateExtruderSettingRow(content, toolchange_group, "retract_length_toolchange", _L("Retraction length"),
                                 extruder_idx);
        CreateExtruderSettingRow(content, toolchange_group, "retract_restart_extra_toolchange", _L("Restart extra"),
                                 extruder_idx);
        sizer->Add(toolchange_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

void PrinterSettingsPanel::OnSettingChanged(const std::string &opt_key)
{
    if (m_disable_update)
        return;

    m_disable_update = true;

    auto it = m_setting_controls.find(opt_key);
    if (it == m_setting_controls.end())
    {
        m_disable_update = false;
        return;
    }

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
    {
        m_disable_update = false;
        return;
    }

    // Use printers config
    DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;

    switch (opt_def->type)
    {
    case coBool:
        if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
        {
            config.set_key_value(opt_key, new ConfigOptionBool(cb->GetValue()));
        }
        break;
    case coInt:
        if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
        {
            config.set_key_value(opt_key, new ConfigOptionInt(spin->GetValue()));
        }
        break;
    case coEnum:
        if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
        {
            int sel = combo->GetSelection();
            if (sel != wxNOT_FOUND && opt_def->enum_def && opt_def->enum_def->has_values())
            {
                const auto &values = opt_def->enum_def->values();
                if (sel < static_cast<int>(values.size()))
                {
                    config.set_deserialize_strict(opt_key, values[sel]);
                }
            }
        }
        break;
    case coFloat:
    case coFloatOrPercent:
    case coPercent:
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            std::string value_str = into_u8(text_input->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            std::string value_str = into_u8(text->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        break;
    case coFloats:
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            // Only update the first value (normal mode), preserve other values
            double new_value = 0;
            if (text_input->GetValue().ToDouble(&new_value))
            {
                auto *opt = config.option<ConfigOptionFloats>(opt_key, true);
                if (opt && !opt->values.empty())
                {
                    opt->values[0] = new_value;
                }
            }
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            // Only update the first value (normal mode), preserve other values
            double new_value = 0;
            if (text->GetValue().ToDouble(&new_value))
            {
                auto *opt = config.option<ConfigOptionFloats>(opt_key, true);
                if (opt && !opt->values.empty())
                {
                    opt->values[0] = new_value;
                }
            }
        }
        break;
    case coString:
    case coStrings:
    default:
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            std::string value_str = into_u8(text_input->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            std::string value_str = into_u8(text->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        break;
    }

    // Update undo UI
    UpdateUndoUI(opt_key);

    // Mark preset as dirty and update tab
    wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);

    // Sync with Printer Settings tab - reload_config because sidebar and tab
    // share the same config object (load_config would find no diff)
    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
    {
        tab->reload_config();
        tab->update_dirty();
        tab->update_changed_ui();
    }

    // Update machine limits visibility if gcode_flavor changed
    if (opt_key == "gcode_flavor")
    {
        UpdateMachineLimitsVisibility();

        const GCodeFlavor flavor = config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;

        // Silently force TimeEstimateOnly for Klipper/RRF when EmitToGCode is selected
        // Mirrors TabPrinter behavior - these firmwares don't support emitting limits to G-code
        const auto *limits_usage = config.option<ConfigOptionEnum<MachineLimitsUsage>>("machine_limits_usage");
        bool is_emit_to_gcode = limits_usage && limits_usage->value == MachineLimitsUsage::EmitToGCode;
        if ((flavor == gcfKlipper || flavor == gcfRepRapFirmware || flavor == gcfRapid) && is_emit_to_gcode)
        {
            config.set_key_value("machine_limits_usage",
                                 new ConfigOptionEnum<MachineLimitsUsage>(MachineLimitsUsage::TimeEstimateOnly));

            // Update the combo box if it exists
            auto usage_it = m_setting_controls.find("machine_limits_usage");
            if (usage_it != m_setting_controls.end())
            {
                if (auto *combo = dynamic_cast<::ComboBox *>(usage_it->second.control))
                {
                    // TimeEstimateOnly is index 1 (after EmitToGCode which is index 0)
                    combo->SetSelection(static_cast<int>(MachineLimitsUsage::TimeEstimateOnly));
                }
                UpdateUndoUI("machine_limits_usage");
            }
        }

        // Check if stealth mode needs to be disabled for this flavor
        // Only Marlin firmware flavors support stealth mode
        bool supports_stealth = (flavor == gcfMarlinFirmware || flavor == gcfMarlinLegacy);
        bool stealth_enabled = config.opt_bool("silent_mode");

        if (!supports_stealth && stealth_enabled)
        {
            // Show warning and disable stealth mode
            wxString msg = _L("The selected G-code flavor does not support the machine limitation for Stealth mode.\n"
                              "Stealth mode will not be applied and will be disabled.");

            InfoDialog dlg(wxGetApp().mainframe, _L("G-code flavor is switched"), msg);
            dlg.ShowModal();

            // Disable silent_mode
            config.set_key_value("silent_mode", new ConfigOptionBool(false));

            // Update the silent_mode checkbox in the sidebar if it exists
            auto silent_it = m_setting_controls.find("silent_mode");
            if (silent_it != m_setting_controls.end())
            {
                if (auto *cb = dynamic_cast<::CheckBox *>(silent_it->second.control))
                    cb->SetValue(false);
                UpdateUndoUI("silent_mode");
            }

            // Sync with tab
            if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
            {
                tab->reload_config();
                tab->update_dirty();
                tab->update_changed_ui();
            }
        }
    }

    // Update stealth mode note visibility when silent_mode changes
    if (opt_key == "silent_mode")
    {
        UpdateMachineLimitsVisibility();
    }

    // Thumbnails validation - mirrors TabPrinter behavior
    // Validates format string and shows error dialog for invalid values
    if (opt_key == "thumbnails" && config.has("thumbnails_format"))
    {
        std::string thumbnails_val = config.opt_string("thumbnails");
        if (!thumbnails_val.empty())
        {
            auto [thumbnails_list, errors] = GCodeThumbnails::make_and_check_thumbnail_list(thumbnails_val);

            if (errors != enum_bitmask<ThumbnailError>())
            {
                std::string error_str = format(_u8L("Invalid value provided for parameter %1%: %2%"), "thumbnails",
                                               thumbnails_val);
                error_str += GCodeThumbnails::get_error_string(errors);
                InfoDialog(wxGetApp().mainframe, _L("Invalid thumbnail format"), from_u8(error_str)).ShowModal();
            }
        }
    }

    // Handle single_extruder_multi_material nozzle diameter equalization dialog
    // Mirrors TabPrinter behavior - when SEMM enabled with multiple extruders,
    // check if nozzle diameters/high_flow values match
    if (opt_key == "single_extruder_multi_material")
    {
        bool semm_enabled = config.opt_bool("single_extruder_multi_material");
        if (semm_enabled && m_extruders_count > 1)
        {
            auto *nozzle_opt = config.option<ConfigOptionFloats>("nozzle_diameter");
            auto *high_flow_opt = config.option<ConfigOptionBools>("nozzle_high_flow");

            if (nozzle_opt && high_flow_opt && nozzle_opt->values.size() > 1)
            {
                bool needs_equalize = false;
                for (size_t i = 1; i < nozzle_opt->values.size(); ++i)
                {
                    if (std::fabs(nozzle_opt->values[i] - nozzle_opt->values[0]) > EPSILON ||
                        (i < high_flow_opt->values.size() && high_flow_opt->values[i] != high_flow_opt->values[0]))
                    {
                        needs_equalize = true;
                        break;
                    }
                }

                if (needs_equalize)
                {
                    wxString msg_text = _L(
                        "This is a single extruder multimaterial printer, \n"
                        "all extruders must have the same nozzle diameter and 'High flow' state.\n"
                        "Do you want to change these values for all extruders to first extruder values?");

                    MessageDialog dialog(wxGetApp().mainframe, msg_text, _L("Extruder settings do not match"),
                                         wxICON_WARNING | wxYES_NO);

                    if (dialog.ShowModal() == wxID_YES)
                    {
                        // Equalize nozzle diameters and high flow
                        std::vector<double> new_diameters(nozzle_opt->values.size(), nozzle_opt->values[0]);
                        std::vector<unsigned char> new_high_flow(
                            high_flow_opt->values.size(), high_flow_opt->values.empty() ? 0 : high_flow_opt->values[0]);

                        config.set_key_value("nozzle_diameter", new ConfigOptionFloats(new_diameters));
                        config.set_key_value("nozzle_high_flow", new ConfigOptionBools(new_high_flow));
                    }
                    else
                    {
                        // User declined - disable SEMM
                        config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(false));

                        // Update checkbox
                        if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
                            cb->SetValue(false);
                        UpdateUndoUI(opt_key);
                    }

                    // Sync with tab
                    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                    {
                        tab->reload_config();
                        tab->update_dirty();
                        tab->update_changed_ui();
                    }
                }
            }
        }

        // Rebuild sidebar tabs to show/hide "Single extruder MM" tab
        CallAfter([this]() { RebuildContent(); });
    }

    // Auto-disable wipe when firmware retraction is enabled - mirrors TabPrinter::toggle_options() behavior
    if (opt_key == "use_firmware_retraction")
    {
        bool use_firmware_retraction = config.opt_bool("use_firmware_retraction");
        if (use_firmware_retraction)
        {
            // Check if any extruder has wipe enabled and disable it
            auto *wipe_opt = config.option<ConfigOptionBools>("wipe", true);
            if (wipe_opt)
            {
                bool wipe_was_enabled = false;
                for (size_t i = 0; i < wipe_opt->values.size(); ++i)
                {
                    if (wipe_opt->values[i])
                    {
                        wipe_opt->values[i] = false;
                        wipe_was_enabled = true;
                    }
                }

                if (wipe_was_enabled)
                {
                    // Update wipe UI controls for all extruders
                    for (size_t i = 0; i < m_extruders_count; ++i)
                    {
                        std::string wipe_key = "wipe_" + std::to_string(i);
                        auto wipe_it = m_setting_controls.find(wipe_key);
                        if (wipe_it != m_setting_controls.end())
                        {
                            if (auto *cb = dynamic_cast<::CheckBox *>(wipe_it->second.control))
                                cb->SetValue(false);
                            UpdateUndoUI(wipe_key);
                        }
                    }

                    // Sync with tab
                    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                    {
                        tab->reload_config();
                        tab->update_dirty();
                        tab->update_changed_ui();
                    }
                }
            }
        }
    }

    // Trigger plater update
    if (GetPlater())
    {
        GetPlater()->on_config_change(config);
    }

    // Apply toggle logic to enable/disable dependent options
    ApplyToggleLogic();

    m_disable_update = false;
}

void PrinterSettingsPanel::UpdateUndoUI(const std::string &opt_key)
{
    auto it = m_setting_controls.find(opt_key);
    if (it == m_setting_controls.end())
        return;

    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const std::string &original_value = it->second.original_value;
    std::string current_value;

    // Check if this is an extruder-specific key (has # suffix)
    size_t hash_pos = opt_key.find('#');
    if (hash_pos != std::string::npos)
    {
        std::string base_key = opt_key.substr(0, hash_pos);
        size_t extruder_idx = std::stoul(opt_key.substr(hash_pos + 1));

        const ConfigOptionDef *opt_def = print_config_def.get(base_key);
        if (opt_def && config.has(base_key))
        {
            switch (opt_def->type)
            {
            case coFloats:
            case coPercents:
                if (auto *opt = config.option<ConfigOptionFloats>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                        current_value = into_u8(wxString::Format("%g", opt->values[extruder_idx]));
                }
                break;
            case coBools:
                if (auto *opt = config.option<ConfigOptionBools>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                        current_value = opt->values[extruder_idx] ? "1" : "0";
                }
                break;
            case coInts:
                if (auto *opt = config.option<ConfigOptionInts>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                        current_value = std::to_string(opt->values[extruder_idx]);
                }
                break;
            case coStrings:
                if (auto *opt = config.option<ConfigOptionStrings>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                        current_value = opt->values[extruder_idx];
                }
                break;
            case coEnums:
                if (auto *opt = dynamic_cast<const ConfigOptionVectorBase *>(config.option(base_key)))
                {
                    if (extruder_idx < opt->size())
                    {
                        auto serialized = opt->vserialize();
                        if (extruder_idx < serialized.size())
                            current_value = serialized[extruder_idx];
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    else
    {
        // Non-extruder-specific key - use standard serialization
        if (config.has(opt_key))
            current_value = config.opt_serialize(opt_key);
    }

    // Check if value differs from original (for undo icon)
    bool is_modified = (current_value != original_value);

    // Update undo icon
    if (auto *bmp = dynamic_cast<wxStaticBitmap *>(it->second.undo_icon))
    {
        if (is_modified)
        {
            bmp->SetBitmap(*get_bmp_bundle("undo"));
            bmp->SetToolTip(_L("Click to revert to original value"));
            bmp->SetCursor(wxCursor(wxCURSOR_HAND));
        }
        else
        {
            bmp->SetBitmap(*get_bmp_bundle("dot"));
            bmp->SetToolTip(wxEmptyString);
            bmp->SetCursor(wxNullCursor);
        }
    }

    // Update lock icon (system preset comparison)
    const Preset *system_preset = nullptr;
    const PresetCollection *presets = &wxGetApp().preset_bundle->printers;
    if (presets)
    {
        const Preset &edited = presets->get_edited_preset();
        if (edited.is_system)
            system_preset = &edited;
        else if (!edited.inherits().empty())
            system_preset = presets->find_preset(edited.inherits(), false);
    }

    bool differs_from_system = true; // Default to different if no system preset
    if (system_preset)
    {
        std::string system_value;
        if (hash_pos != std::string::npos)
        {
            std::string base_key = opt_key.substr(0, hash_pos);
            size_t extruder_idx = std::stoul(opt_key.substr(hash_pos + 1));
            const ConfigOptionDef *opt_def = print_config_def.get(base_key);
            if (opt_def && system_preset->config.has(base_key))
            {
                switch (opt_def->type)
                {
                case coFloats:
                case coPercents:
                    if (auto *opt = system_preset->config.option<ConfigOptionFloats>(base_key))
                    {
                        if (extruder_idx < opt->values.size())
                            system_value = into_u8(wxString::Format("%g", opt->values[extruder_idx]));
                    }
                    break;
                case coBools:
                    if (auto *opt = system_preset->config.option<ConfigOptionBools>(base_key))
                    {
                        if (extruder_idx < opt->values.size())
                            system_value = opt->values[extruder_idx] ? "1" : "0";
                    }
                    break;
                case coInts:
                    if (auto *opt = system_preset->config.option<ConfigOptionInts>(base_key))
                    {
                        if (extruder_idx < opt->values.size())
                            system_value = std::to_string(opt->values[extruder_idx]);
                    }
                    break;
                case coStrings:
                    if (auto *opt = system_preset->config.option<ConfigOptionStrings>(base_key))
                    {
                        if (extruder_idx < opt->values.size())
                            system_value = opt->values[extruder_idx];
                    }
                    break;
                default:
                    break;
                }
            }
        }
        else if (system_preset->config.has(opt_key))
        {
            system_value = system_preset->config.opt_serialize(opt_key);
        }
        differs_from_system = (current_value != system_value);
    }

    if (auto *bmp = dynamic_cast<wxStaticBitmap *>(it->second.lock_icon))
    {
        if (differs_from_system)
        {
            bmp->SetBitmap(*get_bmp_bundle("lock_open"));
            bmp->SetToolTip(_L("Value differs from system preset"));
        }
        else
        {
            bmp->SetBitmap(*get_bmp_bundle("lock_closed"));
            bmp->SetToolTip(_L("Value is same as in the system preset"));
        }
    }
}

void PrinterSettingsPanel::RefreshFromConfig()
{
    m_disable_update = true;

    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;

    // Check if extruder count changed - rebuild sections if needed
    auto *nozzle_opt = config.option<ConfigOptionFloats>("nozzle_diameter");
    size_t new_count = nozzle_opt ? nozzle_opt->values.size() : 1;
    if (new_count != m_extruders_count)
    {
        m_disable_update = false;
        UpdateExtruderCount(new_count);
        m_disable_update = true;
    }

    // Check if Single extruder MM tab visibility changed - rebuild if needed
    bool semm_tab_should_show = ShouldShowSingleExtruderMM();
    bool semm_tab_exists = false;
    for (int i = 0; i < GetTabCount(); ++i)
    {
        if (GetTabName(i) == "single_extruder_mm")
        {
            semm_tab_exists = true;
            break;
        }
    }
    if (semm_tab_should_show != semm_tab_exists)
    {
        m_disable_update = false;
        CallAfter([this]() { RebuildContent(); });
        return; // Let rebuild handle everything
    }

    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        // Check if this is an extruder-specific setting (has # in the key)
        size_t hash_pos = opt_key.find('#');
        if (hash_pos != std::string::npos)
        {
            // Handle extruder-specific settings
            std::string base_key = opt_key.substr(0, hash_pos);
            size_t extruder_idx = std::stoul(opt_key.substr(hash_pos + 1));

            const ConfigOptionDef *opt_def = print_config_def.get(base_key);
            if (!opt_def || !config.has(base_key))
                continue;

            switch (opt_def->type)
            {
            case coBools:
                if (auto *cb = dynamic_cast<::CheckBox *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionBools>(base_key);
                    if (opt && extruder_idx < opt->values.size())
                    {
                        cb->SetValue(opt->values[extruder_idx]);
                    }
                }
                break;
            case coFloats:
            case coPercents:
                if (auto *text_input = dynamic_cast<::TextInput *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionFloats>(base_key);
                    if (opt && extruder_idx < opt->values.size())
                    {
                        text_input->SetValue(wxString::Format("%g", opt->values[extruder_idx]));
                    }
                }
                else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionFloats>(base_key);
                    if (opt && extruder_idx < opt->values.size())
                    {
                        text->SetValue(wxString::Format("%g", opt->values[extruder_idx]));
                    }
                }
                break;
            case coInts:
                if (auto *spin = dynamic_cast<SpinInput *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionInts>(base_key);
                    if (opt && extruder_idx < opt->values.size())
                    {
                        spin->SetValue(opt->values[extruder_idx]);
                    }
                }
                break;
            case coStrings:
                if (base_key == "extruder_colour")
                {
                    if (auto *btn = dynamic_cast<wxButton *>(ui_elem.control))
                    {
                        auto *opt = config.option<ConfigOptionStrings>(base_key);
                        if (opt && extruder_idx < opt->values.size())
                        {
                            wxColour color = opt->values[extruder_idx].empty()
                                                 ? *wxWHITE
                                                 : wxColour(from_u8(opt->values[extruder_idx]));
                            btn->SetBackgroundColour(color);
                            btn->Refresh();
                        }
                    }
                }
                else if (auto *text_input = dynamic_cast<::TextInput *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionStrings>(base_key);
                    if (opt && extruder_idx < opt->values.size())
                    {
                        text_input->SetValue(from_u8(opt->values[extruder_idx]));
                    }
                }
                else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionStrings>(base_key);
                    if (opt && extruder_idx < opt->values.size())
                    {
                        text->SetValue(from_u8(opt->values[extruder_idx]));
                    }
                }
                break;
            case coEnums:
                if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
                {
                    if (opt_def->enum_def && opt_def->enum_def->has_values())
                    {
                        const ConfigOption *raw_opt = config.option(base_key);
                        if (raw_opt)
                        {
                            auto *vec_opt = dynamic_cast<const ConfigOptionVectorBase *>(raw_opt);
                            if (vec_opt && extruder_idx < vec_opt->size())
                            {
                                std::vector<std::string> serialized = vec_opt->vserialize();
                                if (extruder_idx < serialized.size())
                                {
                                    const auto &values = opt_def->enum_def->values();
                                    for (size_t idx = 0; idx < values.size(); ++idx)
                                    {
                                        if (values[idx] == serialized[extruder_idx])
                                        {
                                            combo->SetSelection(static_cast<int>(idx));
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                break;
            default:
                break;
            }

            // Update undo UI for this extruder-specific setting
            UpdateUndoUI(opt_key);
        }
        else
        {
            // Handle regular (non-extruder-specific) settings
            const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
            if (!opt_def || !config.has(opt_key))
                continue;

            // Note: Do NOT update original_value here - it should only be set when
            // the control is created or when a preset is loaded/saved

            switch (opt_def->type)
            {
            case coBool:
                if (auto *cb = dynamic_cast<::CheckBox *>(ui_elem.control))
                {
                    cb->SetValue(config.opt_bool(opt_key));
                }
                break;
            case coInt:
                if (auto *spin = dynamic_cast<SpinInput *>(ui_elem.control))
                {
                    spin->SetValue(config.opt_int(opt_key));
                }
                break;
            case coEnum:
                if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
                {
                    if (opt_def->enum_def && opt_def->enum_def->has_values())
                    {
                        std::string current = config.opt_serialize(opt_key);
                        const auto &values = opt_def->enum_def->values();
                        for (size_t idx = 0; idx < values.size(); ++idx)
                        {
                            if (values[idx] == current)
                            {
                                combo->SetSelection(static_cast<int>(idx));
                                break;
                            }
                        }
                    }
                }
                break;
            case coFloats:
                if (auto *text_input = dynamic_cast<::TextInput *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionFloats>(opt_key);
                    if (opt && !opt->values.empty())
                    {
                        text_input->SetValue(wxString::Format("%g", opt->values[0]));
                    }
                }
                else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionFloats>(opt_key);
                    if (opt && !opt->values.empty())
                    {
                        text->SetValue(wxString::Format("%g", opt->values[0]));
                    }
                }
                break;
            default:
                if (auto *text_input = dynamic_cast<::TextInput *>(ui_elem.control))
                {
                    text_input->SetValue(from_u8(config.opt_serialize(opt_key)));
                }
                else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
                {
                    text->SetValue(from_u8(config.opt_serialize(opt_key)));
                }
                break;
            }

            UpdateUndoUI(opt_key);
        }
    }

    // Update machine limits panel visibility based on gcode_flavor
    UpdateMachineLimitsVisibility();

    // Apply toggle logic to enable/disable dependent options
    ApplyToggleLogic();

    // UNIFIED THEMING: Call SysColorsChanged on parent controls (TextInput, SpinInput, ComboBox)
    // These controls contain ThemedTextCtrl and handle their own color management via WM_CTLCOLOREDIT
    for (auto &[key, ui_elem] : m_setting_controls)
    {
        // Check for our custom controls first - they handle their own theming
        if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
        {
            // Plain wxTextCtrl (not inside our custom controls) - apply colors directly
#ifdef _WIN32
            bool is_editable = text->IsEditable();
            // Disable visual styles so SetBackgroundColour works properly
            SetWindowTheme(text->GetHWND(), L"", L"");
            text->SetBackgroundColour(is_editable ? SidebarColors::InputBackground()
                                                  : SidebarColors::DisabledBackground());
            text->SetForegroundColour(is_editable ? SidebarColors::InputForeground()
                                                  : SidebarColors::DisabledForeground());
            RedrawWindow(text->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
#endif
        }
    }

    m_disable_update = false;
}

void PrinterSettingsPanel::ToggleOption(const std::string &opt_key, bool enable)
{
    auto it = m_setting_controls.find(opt_key);
    if (it != m_setting_controls.end())
        ToggleOptionControl(it->second.control, enable);
}

void PrinterSettingsPanel::ToggleExtruderOption(const std::string &opt_key, size_t extruder_idx, bool enable)
{
    // Extruder-specific options are stored with the key format: "base_key#idx"
    std::string full_key = opt_key + "#" + std::to_string(extruder_idx);
    ToggleOption(full_key, enable);
}

void PrinterSettingsPanel::ApplyToggleLogic()
{
    // Get current config - mirrors TabPrinter::toggle_options()
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;

    // General panel toggles
    bool have_multiple_extruders = m_extruders_count > 1;
    ToggleOption("toolchange_gcode", have_multiple_extruders);
    ToggleOption("single_extruder_multi_material", have_multiple_extruders);

    // Silent mode only available for Marlin firmwares
    const GCodeFlavor flavor = config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;
    bool is_marlin_flavor = flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware;
    ToggleOption("silent_mode", is_marlin_flavor);

    // Filter machine_limits_usage combo - Klipper/RRF/Rapid don't support "Emit to G-code"
    // Mirrors TabPrinter::toggle_options() behavior
    bool emit_to_gcode_available = (flavor != gcfKlipper && flavor != gcfRepRapFirmware && flavor != gcfRapid);
    auto usage_it = m_setting_controls.find("machine_limits_usage");
    if (usage_it != m_setting_controls.end())
    {
        if (auto *combo = dynamic_cast<::ComboBox *>(usage_it->second.control))
        {
            wxString current_value = combo->GetValue();
            combo->Clear();
            if (emit_to_gcode_available)
            {
                combo->Append(_L("Emit to G-code"));
            }
            combo->Append(_L("Use for time estimate"));
            combo->Append(_L("Ignore"));

            // Find matching selection or default to "Use for time estimate"
            int sel = wxNOT_FOUND;
            for (unsigned int i = 0; i < combo->GetCount(); ++i)
            {
                if (combo->GetString(i) == current_value)
                {
                    sel = static_cast<int>(i);
                    break;
                }
            }
            if (sel == wxNOT_FOUND)
            {
                sel = emit_to_gcode_available ? 1 : 0; // "Use for time estimate"
            }
            combo->SetSelection(sel);
        }
    }

    // Machine limits toggle based on machine_limits_usage - mirrors TabPrinter::toggle_options()
    const auto *machine_limits_usage = config.option<ConfigOptionEnum<MachineLimitsUsage>>("machine_limits_usage");
    bool limits_enabled = machine_limits_usage && machine_limits_usage->value != MachineLimitsUsage::Ignore;
    for (const std::string &opt : Preset::machine_limits_options())
        ToggleOption(opt, limits_enabled);

    // Firmware-specific machine limits - mirrors TabPrinter::update_fff() behavior
    // Minimum feedrates only supported by Marlin firmwares
    bool supports_min_feedrates = is_marlin_flavor;
    ToggleOption("machine_min_extruding_rate", limits_enabled && supports_min_feedrates);
    ToggleOption("machine_min_travel_rate", limits_enabled && supports_min_feedrates);

    // Travel acceleration supported by Marlin, RRF, Rapid
    bool supports_travel_acceleration = (flavor == gcfMarlinFirmware || flavor == gcfRepRapFirmware ||
                                         flavor == gcfRapid);
    ToggleOption("machine_max_acceleration_travel", limits_enabled && supports_travel_acceleration);

    // Check if firmware retraction is enabled (affects all extruders)
    bool use_firmware_retraction = config.opt_bool("use_firmware_retraction");

    // Per-extruder toggle logic
    for (size_t i = 0; i < m_extruders_count; ++i)
    {
        // Get retract length for this extruder
        bool have_retract_length = config.opt_float("retract_length", i) > 0;

        // Travel ramping lift
        auto *ramping_opt = config.option<ConfigOptionBools>("travel_ramping_lift");
        bool ramping_lift = ramping_opt && i < ramping_opt->values.size() && ramping_opt->values[i];

        // Calculate if Z lift is active (for retract_lift_above/below)
        bool lifts_z = (ramping_lift && config.opt_float("travel_max_lift", i) > 0) ||
                       (!ramping_lift && config.opt_float("retract_lift", i) > 0);

        // Retraction is available when firmware decides it OR we have retract length
        bool retraction = have_retract_length || use_firmware_retraction;

        // retract_length disabled when firmware retraction is on (firmware decides)
        ToggleExtruderOption("retract_length", i, !use_firmware_retraction);

        // retract_lift disabled when ramping lift is on (use travel_max_lift instead)
        ToggleExtruderOption("retract_lift", i, !ramping_lift);

        // Ramping lift sub-options only when ramping is enabled
        ToggleExtruderOption("travel_max_lift", i, ramping_lift);
        ToggleExtruderOption("travel_slope", i, ramping_lift);
        ToggleExtruderOption("travel_lift_before_obstacle", i, ramping_lift);

        // retract_before_travel available when we have retraction
        ToggleExtruderOption("retract_before_travel", i, retraction);

        // retract_layer_change available when retraction is enabled
        ToggleExtruderOption("retract_layer_change", i, retraction);

        // Lift above/below only apply if Z lift is active
        ToggleExtruderOption("retract_lift_above", i, lifts_z);
        ToggleExtruderOption("retract_lift_below", i, lifts_z);

        // Speed options only when NOT using firmware retraction
        for (const char *el : {"retract_speed", "deretract_speed", "retract_restart_extra"})
            ToggleExtruderOption(el, i, retraction && !use_firmware_retraction);

        // Wipe options
        auto *wipe_opt = config.option<ConfigOptionBools>("wipe");
        bool wipe = wipe_opt && i < wipe_opt->values.size() && wipe_opt->values[i];

        // Wipe checkbox disabled when using firmware retraction
        ToggleExtruderOption("wipe", i, !use_firmware_retraction);

        // wipe_extend and wipe_length work for pure wipe moves (always available)
        ToggleExtruderOption("wipe_extend", i, true);
        ToggleExtruderOption("wipe_length", i, true);

        // retract_before_wipe requires wipe enabled AND not firmware retraction
        ToggleExtruderOption("retract_before_wipe", i, wipe && !use_firmware_retraction);

        // Toolchange retraction - only for multiple extruders
        ToggleExtruderOption("retract_length_toolchange", i, have_multiple_extruders);
        bool toolchange_retraction = config.opt_float("retract_length_toolchange", i) > 0;
        ToggleExtruderOption("retract_restart_extra_toolchange", i, have_multiple_extruders && toolchange_retraction);
    }
}

void PrinterSettingsPanel::msw_rescale()
{
    // Update icon sizes and rescale controls for DPI scaling
    wxSize icon_size = GetScaledIconSizeWx();
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (ui_elem.lock_icon)
            ui_elem.lock_icon->SetMinSize(icon_size);
        if (ui_elem.undo_icon)
            ui_elem.undo_icon->SetMinSize(icon_size);
        // Rescale SpinInput controls so internal buttons reposition correctly
        if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->Rescale();
    }

    TabbedSettingsPanel::msw_rescale();
}

void PrinterSettingsPanel::sys_color_changed()
{
    TabbedSettingsPanel::sys_color_changed();

    // Get current theme background color
    wxColour bg_color = SidebarColors::Background();

    // Refresh ALL setting controls for the new theme
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (!ui_elem.control)
            continue;

        // Update label background color
        if (ui_elem.label_text)
            ui_elem.label_text->SetBackgroundColour(bg_color);

        // Update icon background colors
        if (ui_elem.lock_icon)
            ui_elem.lock_icon->SetBackgroundColour(bg_color);
        if (ui_elem.undo_icon)
            ui_elem.undo_icon->SetBackgroundColour(bg_color);

        // Handle all custom widget types that have SysColorsChanged/sys_color_changed
        if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *checkbox = dynamic_cast<::CheckBox *>(ui_elem.control))
            checkbox->sys_color_changed();

        UpdateUndoUI(opt_key);
    }

    // Update all ScalableButtons (like "Apply below settings to other extruders")
    UpdateScalableButtonsRecursive(this);
}

void PrinterSettingsPanel::OnSysColorChanged()
{
    // Update all setting controls - call SysColorsChanged() on each custom widget
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (!ui_elem.control)
            continue;

        // Try each custom widget type that has SysColorsChanged/sys_color_changed
        if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *checkbox = dynamic_cast<::CheckBox *>(ui_elem.control))
            checkbox->sys_color_changed();
    }
}

// ============================================================================
// FilamentSettingsPanel Implementation - Filament settings with tabbed categories
// ============================================================================

FilamentSettingsPanel::FilamentSettingsPanel(wxWindow *parent, Plater *plater) : TabbedSettingsPanel(parent, plater)
{
    BuildUI();
}

DynamicPrintConfig &FilamentSettingsPanel::GetEditedConfig()
{
    return wxGetApp().preset_bundle->filaments.get_edited_preset().config;
}

const DynamicPrintConfig &FilamentSettingsPanel::GetEditedConfig() const
{
    return wxGetApp().preset_bundle->filaments.get_edited_preset().config;
}

const Preset *FilamentSettingsPanel::GetSystemPresetParent() const
{
    return wxGetApp().preset_bundle->filaments.get_selected_preset_parent();
}

Tab *FilamentSettingsPanel::GetSyncTab() const
{
    return wxGetApp().get_tab(Preset::TYPE_FILAMENT);
}

std::vector<TabbedSettingsPanel::TabDefinition> FilamentSettingsPanel::GetTabDefinitions()
{
    return {{"filament", _L("Filament"), "spool"},
            {"cooling", _L("Cooling"), "cooling"},
            {"advanced", _L("Advanced"), "wrench"},
            {"overrides", _L("Filament Overrides"), "wrench"}};
}

bool FilamentSettingsPanel::IsTabVisible(int tab_index) const
{
    switch (tab_index)
    {
    case TAB_FILAMENT:
        return has_any_visible_setting(
            {"filament_colour", "filament_diameter", "extrusion_multiplier", "filament_density", "filament_cost",
             "filament_spool_weight", "idle_temperature", "first_layer_temperature", "temperature",
             "first_layer_bed_temperature", "bed_temperature", "chamber_temperature", "chamber_minimal_temperature"});

    case TAB_COOLING:
        return has_any_visible_setting({"fan_always_on",
                                        "cooling",
                                        "cooling_slowdown_logic",
                                        "cooling_perimeter_transition_distance",
                                        "min_fan_speed",
                                        "max_fan_speed",
                                        "disable_fan_first_layers",
                                        "full_fan_speed_layer",
                                        "enable_manual_fan_speeds",
                                        "manual_fan_speed_perimeter",
                                        "manual_fan_speed_external_perimeter",
                                        "manual_fan_speed_overhang_perimeter",
                                        "manual_fan_speed_interlocking_perimeter",
                                        "manual_fan_speed_internal_infill",
                                        "manual_fan_speed_solid_infill",
                                        "bridge_fan_speed",
                                        "manual_fan_speed_top_solid_infill",
                                        "manual_fan_speed_ironing",
                                        "manual_fan_speed_gap_fill",
                                        "manual_fan_speed_skirt",
                                        "manual_fan_speed_support_material",
                                        "manual_fan_speed_support_interface",
                                        "enable_dynamic_fan_speeds",
                                        "overhang_fan_speed_0",
                                        "overhang_fan_speed_1",
                                        "overhang_fan_speed_2",
                                        "overhang_fan_speed_3",
                                        "fan_spinup_bridge_infill",
                                        "fan_spinup_overhang_perimeter",
                                        "fan_below_layer_time",
                                        "slowdown_below_layer_time",
                                        "min_print_speed"});

    case TAB_ADVANCED:
        return has_any_visible_setting({"filament_type",
                                        "filament_soluble",
                                        "filament_abrasive",
                                        "filament_max_volumetric_speed",
                                        "filament_infill_max_speed",
                                        "filament_infill_max_crossing_speed",
                                        "filament_shrinkage_compensation_x",
                                        "filament_shrinkage_compensation_y",
                                        "filament_shrinkage_compensation_z",
                                        "filament_minimal_purge_on_wipe_tower",
                                        "filament_loading_speed_start",
                                        "filament_loading_speed",
                                        "filament_unloading_speed_start",
                                        "filament_unloading_speed",
                                        "filament_load_time",
                                        "filament_unload_time",
                                        "filament_toolchange_delay",
                                        "filament_cooling_moves",
                                        "filament_cooling_initial_speed",
                                        "filament_cooling_final_speed",
                                        "filament_stamping_loading_speed",
                                        "filament_stamping_distance",
                                        "filament_purge_multiplier",
                                        "filament_multitool_ramming",
                                        "filament_multitool_ramming_volume",
                                        "filament_multitool_ramming_flow"});

    case TAB_OVERRIDES:
        return has_any_visible_setting({"filament_retract_lift",
                                        "filament_travel_ramping_lift",
                                        "filament_travel_max_lift",
                                        "filament_travel_slope",
                                        "filament_travel_lift_before_obstacle",
                                        "filament_retract_lift_above",
                                        "filament_retract_lift_below",
                                        "filament_retract_length",
                                        "filament_retract_speed",
                                        "filament_deretract_speed",
                                        "filament_retract_restart_extra",
                                        "filament_retract_before_travel",
                                        "filament_retract_layer_change",
                                        "filament_wipe",
                                        "filament_wipe_extend",
                                        "filament_retract_before_wipe",
                                        "filament_wipe_length",
                                        "filament_retract_length_toolchange",
                                        "filament_retract_restart_extra_toolchange",
                                        "filament_seam_gap_distance"});

    default:
        return true;
    }
}

wxPanel *FilamentSettingsPanel::BuildTabContent(int tab_index)
{
    switch (tab_index)
    {
    case TAB_FILAMENT:
        return BuildFilamentContent();
    case TAB_COOLING:
        return BuildCoolingContent();
    case TAB_ADVANCED:
        return BuildAdvancedContent();
    case TAB_OVERRIDES:
        return BuildOverridesContent();
    default:
        return nullptr;
    }
}

wxPanel *FilamentSettingsPanel::BuildFilamentContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Filament group
    if (has_any_visible_setting({"filament_colour", "filament_diameter", "extrusion_multiplier", "filament_density",
                                 "filament_cost", "filament_spool_weight"}))
    {
        auto *filament_group = CreateFlatStaticBoxSizer(content, _L("Filament"));
        CreateSettingRow(content, filament_group, "filament_colour", _L("Color"));
        CreateSettingRow(content, filament_group, "filament_diameter", _L("Diameter"));
        CreateSettingRow(content, filament_group, "extrusion_multiplier", _L("Extrusion multiplier"));
        CreateSettingRow(content, filament_group, "filament_density", _L("Density"));
        CreateSettingRow(content, filament_group, "filament_cost", _L("Cost"));
        CreateSettingRow(content, filament_group, "filament_spool_weight", _L("Spool weight"));
        sizer->Add(filament_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Temperature group
    if (has_any_visible_setting({"idle_temperature", "first_layer_temperature", "temperature",
                                 "first_layer_bed_temperature", "bed_temperature", "chamber_temperature",
                                 "chamber_minimal_temperature"}))
    {
        auto *temp_group = CreateFlatStaticBoxSizer(content, _L("Temperature"));
        CreateNullableSettingRow(content, temp_group, "idle_temperature", _L("Idle temperature"));
        CreateSettingRow(content, temp_group, "first_layer_temperature", _L("First layer nozzle"));
        CreateSettingRow(content, temp_group, "temperature", _L("Other layers nozzle"));
        CreateSettingRow(content, temp_group, "first_layer_bed_temperature", _L("First layer bed"));
        CreateSettingRow(content, temp_group, "bed_temperature", _L("Other layers bed"));
        CreateSettingRow(content, temp_group, "chamber_temperature", _L("Chamber"));
        CreateSettingRow(content, temp_group, "chamber_minimal_temperature", _L("Chamber minimal"));
        sizer->Add(temp_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *FilamentSettingsPanel::BuildCoolingContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Enable group
    if (has_any_visible_setting(
            {"fan_always_on", "cooling", "cooling_slowdown_logic", "cooling_perimeter_transition_distance"}))
    {
        auto *enable_group = CreateFlatStaticBoxSizer(content, _L("Enable"));
        CreateSettingRow(content, enable_group, "fan_always_on", _L("Keep fan always on"));
        CreateSettingRow(content, enable_group, "cooling", _L("Enable auto cooling"));
        CreateSettingRow(content, enable_group, "cooling_slowdown_logic", _L("Slowdown logic"));
        CreateSettingRow(content, enable_group, "cooling_perimeter_transition_distance",
                         _L("Perimeter transition distance"));
        sizer->Add(enable_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Fan settings group
    if (has_any_visible_setting({"min_fan_speed", "max_fan_speed", "disable_fan_first_layers", "full_fan_speed_layer"}))
    {
        auto *fan_group = CreateFlatStaticBoxSizer(content, _L("Fan settings"));
        CreateSettingRow(content, fan_group, "min_fan_speed", _L("Min fan speed"));
        CreateSettingRow(content, fan_group, "max_fan_speed", _L("Max fan speed"));
        CreateSettingRow(content, fan_group, "disable_fan_first_layers", _L("Disable fan for first"));
        CreateSettingRow(content, fan_group, "full_fan_speed_layer", _L("Full fan speed at layer"));
        sizer->Add(fan_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Manual fan controls group
    if (has_any_visible_setting(
            {"enable_manual_fan_speeds", "manual_fan_speed_perimeter", "manual_fan_speed_external_perimeter",
             "manual_fan_speed_overhang_perimeter", "manual_fan_speed_interlocking_perimeter",
             "manual_fan_speed_internal_infill", "manual_fan_speed_solid_infill", "bridge_fan_speed",
             "manual_fan_speed_top_solid_infill", "manual_fan_speed_ironing", "manual_fan_speed_gap_fill",
             "manual_fan_speed_skirt", "manual_fan_speed_support_material", "manual_fan_speed_support_interface"}))
    {
        auto *manual_group = CreateFlatStaticBoxSizer(content, _L("Manual fan controls"));
        CreateSettingRow(content, manual_group, "enable_manual_fan_speeds", _L("Enable manual fan speeds"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_perimeter", _L("Perimeter"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_external_perimeter", _L("External perimeter"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_overhang_perimeter", _L("Overhang perimeter"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_interlocking_perimeter",
                         _L("Interlocking perimeter"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_internal_infill", _L("Internal infill"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_solid_infill", _L("Solid infill"));
        CreateSettingRow(content, manual_group, "bridge_fan_speed", _L("Bridge"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_top_solid_infill", _L("Top solid infill"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_ironing", _L("Ironing"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_gap_fill", _L("Gap fill"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_skirt", _L("Skirt"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_support_material", _L("Support material"));
        CreateSettingRow(content, manual_group, "manual_fan_speed_support_interface", _L("Support interface"));
        sizer->Add(manual_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Dynamic fan speeds group
    if (has_any_visible_setting({"enable_dynamic_fan_speeds", "overhang_fan_speed_0", "overhang_fan_speed_1",
                                 "overhang_fan_speed_2", "overhang_fan_speed_3"}))
    {
        auto *dynamic_group = CreateFlatStaticBoxSizer(content, _L("Dynamic fan speeds"));
        CreateSettingRow(content, dynamic_group, "enable_dynamic_fan_speeds", _L("Enable dynamic fan speeds"));
        CreateSettingRow(content, dynamic_group, "overhang_fan_speed_0", _L("Overhang fan speed 0%"));
        CreateSettingRow(content, dynamic_group, "overhang_fan_speed_1", _L("Overhang fan speed 25%"));
        CreateSettingRow(content, dynamic_group, "overhang_fan_speed_2", _L("Overhang fan speed 50%"));
        CreateSettingRow(content, dynamic_group, "overhang_fan_speed_3", _L("Overhang fan speed 75%"));
        sizer->Add(dynamic_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Fan spin-up group
    if (has_any_visible_setting({"fan_spinup_bridge_infill", "fan_spinup_overhang_perimeter"}))
    {
        auto *spinup_group = CreateFlatStaticBoxSizer(content, _L("Fan spin-up"));
        CreateSettingRow(content, spinup_group, "fan_spinup_bridge_infill", _L("Bridge infill"));
        CreateSettingRow(content, spinup_group, "fan_spinup_overhang_perimeter", _L("Overhang perimeter"));
        sizer->Add(spinup_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Cooling thresholds group
    if (has_any_visible_setting({"fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed"}))
    {
        auto *threshold_group = CreateFlatStaticBoxSizer(content, _L("Cooling thresholds"));
        CreateSettingRow(content, threshold_group, "fan_below_layer_time", _L("Fan below layer time"));
        CreateSettingRow(content, threshold_group, "slowdown_below_layer_time", _L("Slowdown below layer time"));
        CreateSettingRow(content, threshold_group, "min_print_speed", _L("Min print speed"));
        sizer->Add(threshold_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);

    // Apply toggle logic to set initial enabled/disabled state
    ApplyToggleLogic();

    return content;
}

wxPanel *FilamentSettingsPanel::BuildAdvancedContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Filament properties group
    if (has_any_visible_setting({"filament_type", "filament_soluble", "filament_abrasive"}))
    {
        auto *props_group = CreateFlatStaticBoxSizer(content, _L("Filament properties"));
        CreateSettingRow(content, props_group, "filament_type", _L("Filament type"));
        CreateSettingRow(content, props_group, "filament_soluble", _L("Soluble material"));
        CreateSettingRow(content, props_group, "filament_abrasive", _L("Abrasive material"));
        sizer->Add(props_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Print speed override group
    if (has_any_visible_setting(
            {"filament_max_volumetric_speed", "filament_infill_max_speed", "filament_infill_max_crossing_speed"}))
    {
        auto *speed_group = CreateFlatStaticBoxSizer(content, _L("Print speed override"));
        CreateSettingRow(content, speed_group, "filament_max_volumetric_speed", _L("Max volumetric speed"));
        CreateSettingRow(content, speed_group, "filament_infill_max_speed", _L("Max infill speed"));
        CreateSettingRow(content, speed_group, "filament_infill_max_crossing_speed", _L("Max crossing infill speed"));
        sizer->Add(speed_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Shrinkage compensation group
    if (has_any_visible_setting({"filament_shrinkage_compensation_x", "filament_shrinkage_compensation_y",
                                 "filament_shrinkage_compensation_z"}))
    {
        auto *shrink_group = CreateFlatStaticBoxSizer(content, _L("Shrinkage compensation"));
        CreateSettingRow(content, shrink_group, "filament_shrinkage_compensation_x", _L("X compensation"));
        CreateSettingRow(content, shrink_group, "filament_shrinkage_compensation_y", _L("Y compensation"));
        CreateSettingRow(content, shrink_group, "filament_shrinkage_compensation_z", _L("Z compensation"));
        sizer->Add(shrink_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Wipe tower group
    if (has_any_visible_setting({"filament_minimal_purge_on_wipe_tower"}))
    {
        auto *wipe_group = CreateFlatStaticBoxSizer(content, _L("Wipe tower parameters"));
        CreateSettingRow(content, wipe_group, "filament_minimal_purge_on_wipe_tower",
                         _L("Minimal purge on wipe tower"));
        sizer->Add(wipe_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Toolchange single extruder MM group
    if (has_any_visible_setting(
            {"filament_loading_speed_start", "filament_loading_speed", "filament_unloading_speed_start",
             "filament_unloading_speed", "filament_load_time", "filament_unload_time", "filament_toolchange_delay",
             "filament_cooling_moves", "filament_cooling_initial_speed", "filament_cooling_final_speed",
             "filament_stamping_loading_speed", "filament_stamping_distance", "filament_purge_multiplier"}))
    {
        auto *tc_single_group = CreateFlatStaticBoxSizer(content, _L("Single extruder MMU"));
        CreateSettingRow(content, tc_single_group, "filament_loading_speed_start", _L("Loading speed (start)"));
        CreateSettingRow(content, tc_single_group, "filament_loading_speed", _L("Loading speed"));
        CreateSettingRow(content, tc_single_group, "filament_unloading_speed_start", _L("Unloading speed (start)"));
        CreateSettingRow(content, tc_single_group, "filament_unloading_speed", _L("Unloading speed"));
        CreateSettingRow(content, tc_single_group, "filament_load_time", _L("Load time"));
        CreateSettingRow(content, tc_single_group, "filament_unload_time", _L("Unload time"));
        CreateSettingRow(content, tc_single_group, "filament_toolchange_delay", _L("Toolchange delay"));
        CreateSettingRow(content, tc_single_group, "filament_cooling_moves", _L("Cooling moves"));
        CreateSettingRow(content, tc_single_group, "filament_cooling_initial_speed", _L("Cooling initial speed"));
        CreateSettingRow(content, tc_single_group, "filament_cooling_final_speed", _L("Cooling final speed"));
        CreateSettingRow(content, tc_single_group, "filament_stamping_loading_speed", _L("Stamping loading speed"));
        CreateSettingRow(content, tc_single_group, "filament_stamping_distance", _L("Stamping distance"));
        CreateSettingRow(content, tc_single_group, "filament_purge_multiplier", _L("Purge multiplier"));

        // Ramming settings button - opens the existing ramming dialog
        {
            auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

            // Left side: label (50% width)
            auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto *label = new wxStaticText(content, wxID_ANY, _L("Ramming:"), wxDefaultPosition, wxDefaultSize,
                                           wxST_ELLIPSIZE_END);
            label->SetMinSize(wxSize(1, -1));
            label->SetToolTip(_L("Ramming parameters for filament loading"));
            left_sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            row_sizer->Add(left_sizer, 1, wxEXPAND);

            // Right side: button left-justified in value area (50% width)
            auto *right_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto *btn = new ScalableButton(content, wxID_ANY, "settings", _L("Ramming settings"), wxDefaultSize,
                                           wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
            btn->SetToolTip(_L("Open ramming settings editor"));
            btn->Bind(wxEVT_BUTTON,
                      [](wxCommandEvent &)
                      {
                          DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
                          auto *ramming_opt = config.option<ConfigOptionStrings>("filament_ramming_parameters");
                          if (!ramming_opt || ramming_opt->values.empty())
                              return;

                          RammingDialog dlg(wxGetApp().mainframe, ramming_opt->get_at(0));
                          dlg.CentreOnParent();
                          if (dlg.ShowModal() == wxID_OK)
                          {
                              std::vector<std::string> params = ramming_opt->values;
                              params[0] = dlg.get_parameters();
                              config.set_key_value("filament_ramming_parameters", new ConfigOptionStrings(params));

                              wxGetApp().preset_bundle->filaments.get_edited_preset().set_dirty(true);
                              if (auto *tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT))
                              {
                                  tab->reload_config();
                                  tab->update_dirty();
                                  tab->update_changed_ui();
                              }
                              if (wxGetApp().plater())
                                  wxGetApp().plater()->on_config_change(config);
                          }
                      });
            right_sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);
            right_sizer->AddStretchSpacer(1);
            row_sizer->Add(right_sizer, 1, wxEXPAND);

            tc_single_group->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
        }

        sizer->Add(tc_single_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Toolchange multi extruder MM group
    if (has_any_visible_setting(
            {"filament_multitool_ramming", "filament_multitool_ramming_volume", "filament_multitool_ramming_flow"}))
    {
        auto *tc_multi_group = CreateFlatStaticBoxSizer(content, _L("Multi extruder MMU"));
        CreateSettingRow(content, tc_multi_group, "filament_multitool_ramming", _L("Multitool ramming"));
        CreateSettingRow(content, tc_multi_group, "filament_multitool_ramming_volume", _L("Ramming volume"));
        CreateSettingRow(content, tc_multi_group, "filament_multitool_ramming_flow", _L("Ramming flow"));
        sizer->Add(tc_multi_group, 0, wxEXPAND | wxALL, em / 4);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

wxPanel *FilamentSettingsPanel::BuildOverridesContent()
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    // Set theme colors on content panel so child controls inherit them
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    int em = wxGetApp().em_unit();

    // Travel lift group
    if (has_any_visible_setting({"filament_retract_lift", "filament_travel_ramping_lift", "filament_travel_max_lift",
                                 "filament_travel_slope", "filament_travel_lift_before_obstacle",
                                 "filament_retract_lift_above", "filament_retract_lift_below"}))
    {
        auto *lift_group = CreateFlatStaticBoxSizer(content, _L("Travel lift"));
        CreateNullableSettingRow(content, lift_group, "filament_retract_lift", _L("Lift Z"));
        CreateNullableSettingRow(content, lift_group, "filament_travel_ramping_lift", _L("Ramping lift"));
        CreateNullableSettingRow(content, lift_group, "filament_travel_max_lift", _L("Max lift"));
        CreateNullableSettingRow(content, lift_group, "filament_travel_slope", _L("Travel slope"));
        CreateNullableSettingRow(content, lift_group, "filament_travel_lift_before_obstacle",
                                 _L("Lift before obstacle"));
        CreateNullableSettingRow(content, lift_group, "filament_retract_lift_above", _L("Only lift above"));
        CreateNullableSettingRow(content, lift_group, "filament_retract_lift_below", _L("Only lift below"));
        sizer->Add(lift_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Retraction group
    if (has_any_visible_setting({"filament_retract_length", "filament_retract_speed", "filament_deretract_speed",
                                 "filament_retract_restart_extra", "filament_retract_before_travel",
                                 "filament_retract_layer_change", "filament_wipe", "filament_wipe_extend",
                                 "filament_retract_before_wipe", "filament_wipe_length"}))
    {
        auto *retract_group = CreateFlatStaticBoxSizer(content, _L("Retraction"));
        CreateNullableSettingRow(content, retract_group, "filament_retract_length", _L("Retraction length"));
        CreateNullableSettingRow(content, retract_group, "filament_retract_speed", _L("Retraction speed"));
        CreateNullableSettingRow(content, retract_group, "filament_deretract_speed", _L("Deretraction speed"));
        CreateNullableSettingRow(content, retract_group, "filament_retract_restart_extra", _L("Restart extra"));
        CreateNullableSettingRow(content, retract_group, "filament_retract_before_travel", _L("Minimum travel"));
        CreateNullableSettingRow(content, retract_group, "filament_retract_layer_change",
                                 _L("Retract on layer change"));
        CreateNullableSettingRow(content, retract_group, "filament_wipe", _L("Wipe while retracting"));
        CreateNullableSettingRow(content, retract_group, "filament_wipe_extend", _L("Wipe extend"));
        CreateNullableSettingRow(content, retract_group, "filament_retract_before_wipe", _L("Retract before wipe"));
        CreateNullableSettingRow(content, retract_group, "filament_wipe_length", _L("Wipe length"));
        sizer->Add(retract_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Tool change retraction group
    if (has_any_visible_setting({"filament_retract_length_toolchange", "filament_retract_restart_extra_toolchange"}))
    {
        auto *tc_retract_group = CreateFlatStaticBoxSizer(content, _L("Tool change retraction"));
        CreateNullableSettingRow(content, tc_retract_group, "filament_retract_length_toolchange",
                                 _L("Retraction length"));
        CreateNullableSettingRow(content, tc_retract_group, "filament_retract_restart_extra_toolchange",
                                 _L("Restart extra"));
        sizer->Add(tc_retract_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Seams group
    if (has_any_visible_setting({"filament_seam_gap_distance"}))
    {
        auto *seams_group = CreateFlatStaticBoxSizer(content, _L("Seams"));
        CreateNullableSettingRow(content, seams_group, "filament_seam_gap_distance", _L("Seam gap distance"));
        sizer->Add(seams_group, 0, wxEXPAND | wxALL, em / 4);
    }

    // Apply initial toggle state
    UpdateOverridesToggleState();

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

void FilamentSettingsPanel::CreateSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                             const wxString &label, bool full_width)
{
    // Check sidebar visibility - skip if user has hidden this setting
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        return;

    int em = wxGetApp().em_unit();

    // Create the common row header (icons, label, sizers)
    RowUIContext ctx = CreateRowUIBase(parent, opt_key, label);
    if (!ctx.row_sizer)
        return; // Option not found

    const ConfigOptionDef *opt_def = ctx.opt_def;
    wxStaticBitmap *lock_icon = ctx.lock_icon;
    wxStaticBitmap *undo_icon = ctx.undo_icon;
    wxBoxSizer *row_sizer = ctx.row_sizer;
    wxString tooltip = ctx.tooltip;

    wxWindow *value_ctrl = nullptr;
    const DynamicPrintConfig &config = GetEditedConfig();
    std::string original_value;

    switch (opt_def->type)
    {
    case coBool:
    case coBools:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *checkbox = new ::CheckBox(parent);
        checkbox->SetBackgroundColour(SidebarColors::Background());
        if (config.has(opt_key))
        {
            // Handle both coBool and coBools (get first value for coBools)
            if (opt_def->type == coBools)
            {
                auto *opt = config.option<ConfigOptionBools>(opt_key);
                if (opt && !opt->values.empty())
                    checkbox->SetValue(opt->values[0]);
            }
            else
            {
                checkbox->SetValue(config.opt_bool(opt_key));
            }
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            checkbox->SetToolTip(tooltip);

        checkbox->Bind(wxEVT_CHECKBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
        value_sizer->AddStretchSpacer(1);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = checkbox;
        break;
    }

    case coEnum:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Use custom ComboBox widget for proper dark mode support
        // DD_NO_CHECK_ICON removes checkmarks, 16em width matches main tabs
        auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16 * em, -1), 0,
                                     nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
        if (opt_def->enum_def && opt_def->enum_def->has_labels())
        {
            for (const std::string &enum_label : opt_def->enum_def->labels())
            {
                combo->Append(from_u8(enum_label));
            }
        }

        if (config.has(opt_key) && opt_def->enum_def && opt_def->enum_def->has_values())
        {
            original_value = config.opt_serialize(opt_key);
            const auto &values = opt_def->enum_def->values();
            for (size_t idx = 0; idx < values.size(); ++idx)
            {
                if (values[idx] == original_value)
                {
                    combo->SetSelection(static_cast<int>(idx));
                    break;
                }
            }
        }
        if (!tooltip.empty())
            combo->SetToolTip(tooltip);

        combo->Bind(wxEVT_COMBOBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(combo, 0, wxALIGN_CENTER_VERTICAL); // Fixed 16em width (matches main tabs)
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = combo;
        break;
    }

    case coEnums:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Use custom ComboBox widget for proper dark mode support
        // DD_NO_CHECK_ICON removes checkmarks, 16em width matches main tabs
        auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16 * em, -1), 0,
                                     nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
        if (opt_def->enum_def && opt_def->enum_def->has_labels())
        {
            for (const std::string &enum_label : opt_def->enum_def->labels())
            {
                combo->Append(from_u8(enum_label));
            }
        }

        if (config.has(opt_key) && opt_def->enum_def && opt_def->enum_def->has_values())
        {
            // For vector enums, use vserialize() to get string values (use first value)
            const ConfigOption *opt = config.option(opt_key);
            if (opt)
            {
                auto *vec_opt = dynamic_cast<const ConfigOptionVectorBase *>(opt);
                if (vec_opt && vec_opt->size() > 0)
                {
                    std::vector<std::string> serialized = vec_opt->vserialize();
                    if (!serialized.empty())
                    {
                        original_value = serialized[0];
                        // Find matching value in enum values list
                        const auto &values = opt_def->enum_def->values();
                        for (size_t idx = 0; idx < values.size(); ++idx)
                        {
                            if (values[idx] == original_value)
                            {
                                combo->SetSelection(static_cast<int>(idx));
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (!tooltip.empty())
            combo->SetToolTip(tooltip);

        combo->Bind(wxEVT_COMBOBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(combo, 0, wxALIGN_CENTER_VERTICAL); // Fixed 16em width (matches main tabs)
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = combo;
        break;
    }

    case coInt:
    case coInts:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        int min_val = opt_def->min > INT_MIN ? static_cast<int>(opt_def->min) : 0;
        int max_val = opt_def->max < INT_MAX ? static_cast<int>(opt_def->max) : 10000;
        int value = 0;
        if (config.has(opt_key))
        {
            if (opt_def->type == coInts)
            {
                auto *opt = config.option<ConfigOptionInts>(opt_key);
                if (opt && !opt->values.empty())
                    value = opt->values[0];
            }
            else
            {
                value = config.opt_int(opt_key);
            }
            original_value = config.opt_serialize(opt_key);
        }

        wxString text_value = wxString::Format("%d", value);

        // Simple creation - 70px width like Tab.cpp, left aligned
        auto *spin = new SpinInput(parent, text_value, "", wxDefaultPosition, wxSize(GetScaledInputWidth(), -1), 0,
                                   min_val, max_val, value);

        if (opt_def->step > 1)
            spin->SetStep(static_cast<int>(opt_def->step));

        if (!tooltip.empty())
            spin->SetToolTip(tooltip);

        spin->Bind(wxEVT_SPINCTRL, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL);

        // Add sidetext (units) if available - strip parenthetical notes for compact display
        if (!opt_def->sidetext.empty())
        {
            std::string sidetext = opt_def->sidetext;
            size_t paren_pos = sidetext.find('(');
            if (paren_pos != std::string::npos)
                sidetext = sidetext.substr(0, paren_pos);
            boost::trim(sidetext);
            if (!sidetext.empty())
            {
                auto *unit_text = new wxStaticText(parent, wxID_ANY, from_u8(sidetext));
                value_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            }
        }

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = spin;
        break;
    }

    case coFloat:
    case coFloats:
    case coFloatOrPercent:
    case coFloatsOrPercents:
    case coPercent:
    case coPercents:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition,
                                     wxSize(GetScaledInputWidth(), -1));
        wxGetApp().UpdateDarkUI(text);

        if (config.has(opt_key))
        {
            wxString value_str = from_u8(config.opt_serialize(opt_key));
            text->SetValue(value_str);
            original_value = config.opt_serialize(opt_key);
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL); // Fixed 60px width

        // Add sidetext (units) if available - strip parenthetical notes for compact display
        if (!opt_def->sidetext.empty())
        {
            std::string sidetext = opt_def->sidetext;
            size_t paren_pos = sidetext.find('(');
            if (paren_pos != std::string::npos)
                sidetext = sidetext.substr(0, paren_pos);
            boost::trim(sidetext);
            if (!sidetext.empty())
            {
                auto *unit_text = new wxStaticText(parent, wxID_ANY, from_u8(sidetext));
                value_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            }
        }

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }

    case coString:
    case coStrings:
    default:
    {
        // select_open: string option with suggested dropdown values (e.g. filament_type)
        if (opt_def->gui_type == ConfigOptionDef::GUIType::select_open && opt_def->enum_def &&
            opt_def->enum_def->has_values())
        {
            auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16 * em, -1), 0,
                                         nullptr, DD_NO_CHECK_ICON);

            for (const std::string &val : opt_def->enum_def->values())
                combo->Append(from_u8(val));

            if (config.has(opt_key))
            {
                original_value = config.opt_serialize(opt_key);
                // Find and select the matching value
                for (size_t idx = 0; idx < opt_def->enum_def->values().size(); ++idx)
                {
                    if (opt_def->enum_def->values()[idx] == original_value)
                    {
                        combo->SetSelection(static_cast<int>(idx));
                        break;
                    }
                }
            }
            if (!tooltip.empty())
                combo->SetToolTip(tooltip);

            combo->Bind(wxEVT_COMBOBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });
            combo->Bind(wxEVT_KILL_FOCUS,
                        [this, opt_key](wxFocusEvent &evt)
                        {
                            OnSettingChanged(opt_key);
                            evt.Skip();
                        });

            value_sizer->Add(combo, 0, wxALIGN_CENTER_VERTICAL);
            row_sizer->Add(value_sizer, 1, wxEXPAND);
            value_ctrl = combo;
        }
        // Special handling for filament_colour with color picker
        else if (opt_key == "filament_colour")
        {
            auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);

            // Get current color
            wxColour current_color = *wxWHITE;
            if (config.has(opt_key))
            {
                auto *opt = config.option<ConfigOptionStrings>(opt_key);
                if (opt && !opt->values.empty() && !opt->values[0].empty())
                {
                    wxColour clr(from_u8(opt->values[0]));
                    if (clr.IsOk())
                        current_color = clr;
                    original_value = opt->values[0];
                }
            }

            // Use a simple panel with border that shows color and opens dialog on click
            auto *color_panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(em * 6, em * 2),
                                            wxBORDER_SIMPLE);
            color_panel->SetBackgroundColour(current_color);
            color_panel->SetCursor(wxCursor(wxCURSOR_HAND));

            color_panel->Bind(wxEVT_LEFT_DOWN,
                              [this, color_panel, opt_key](wxMouseEvent &)
                              {
                                  wxColourData data;
                                  data.SetColour(color_panel->GetBackgroundColour());
                                  wxColourDialog dlg(wxGetApp().mainframe, &data);
                                  dlg.CentreOnParent();
                                  if (dlg.ShowModal() == wxID_OK)
                                  {
                                      wxColour new_color = dlg.GetColourData().GetColour();
                                      color_panel->SetBackgroundColour(new_color);
                                      color_panel->Refresh();
                                      OnSettingChanged(opt_key);
                                  }
                              });

            value_sizer->Add(color_panel, 0, wxALIGN_CENTER_VERTICAL);
            value_sizer->AddStretchSpacer(1);
            row_sizer->Add(value_sizer, 1, wxEXPAND);
            value_ctrl = color_panel;
        }
        else
        {
            auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, wxDefaultSize);
            wxGetApp().UpdateDarkUI(text);
            text->SetMinSize(wxSize(1, -1)); // Allow text to shrink

            if (config.has(opt_key))
            {
                wxString value_str = from_u8(config.opt_serialize(opt_key));
                text->SetValue(value_str);
                original_value = config.opt_serialize(opt_key);
            }
            if (!tooltip.empty())
                text->SetToolTip(tooltip);

            text->Bind(wxEVT_KILL_FOCUS,
                       [this, opt_key](wxFocusEvent &evt)
                       {
                           OnSettingChanged(opt_key);
                           evt.Skip();
                       });

            value_sizer->Add(text, 1, wxEXPAND); // Always shrink with sizer
            row_sizer->Add(value_sizer, 1, wxEXPAND);
            value_ctrl = text;
        }
        break;
    }
    }

    if (value_ctrl)
    {
        SettingUIElements ui_elem;
        ui_elem.control = value_ctrl;
        ui_elem.lock_icon = lock_icon;
        ui_elem.undo_icon = undo_icon;
        ui_elem.label_text = ctx.label_text;
        ui_elem.original_value = original_value;
        m_setting_controls[opt_key] = ui_elem;

        UpdateUndoUI(opt_key);

        undo_icon->Bind(wxEVT_LEFT_DOWN,
                        [this, opt_key](wxMouseEvent &)
                        {
                            auto it = m_setting_controls.find(opt_key);
                            if (it == m_setting_controls.end())
                                return;

                            const ConfigOptionDef *def = print_config_def.get(opt_key);
                            if (!def)
                                return;

                            switch (def->type)
                            {
                            case coBool:
                            case coBools:
                                if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
                                {
                                    cb->SetValue(it->second.original_value == "1");
                                }
                                break;
                            case coInt:
                            case coInts:
                                if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
                                {
                                    spin->SetValue(std::stoi(it->second.original_value));
                                }
                                break;
                            case coEnum:
                                if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
                                {
                                    if (def->enum_def && def->enum_def->has_values())
                                    {
                                        const auto &values = def->enum_def->values();
                                        for (size_t idx = 0; idx < values.size(); ++idx)
                                        {
                                            if (values[idx] == it->second.original_value)
                                            {
                                                combo->SetSelection(static_cast<int>(idx));
                                                break;
                                            }
                                        }
                                    }
                                }
                                break;
                            default:
                                if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
                                {
                                    // select_open string: revert dropdown selection
                                    if (def->enum_def && def->enum_def->has_values())
                                    {
                                        const auto &values = def->enum_def->values();
                                        for (size_t idx = 0; idx < values.size(); ++idx)
                                        {
                                            if (values[idx] == it->second.original_value)
                                            {
                                                combo->SetSelection(static_cast<int>(idx));
                                                break;
                                            }
                                        }
                                    }
                                }
                                else if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
                                {
                                    text_input->SetValue(from_u8(it->second.original_value));
                                }
                                else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
                                {
                                    text->SetValue(from_u8(it->second.original_value));
                                }
                                break;
                            }

                            OnSettingChanged(opt_key);
                            UpdateUndoUI(opt_key);
                        });
    }

    sizer->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
}

void FilamentSettingsPanel::CreateMultilineSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                                      const wxString &label, int num_lines)
{
    // Check sidebar visibility - skip if user has hidden this setting
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        return;

    int em = wxGetApp().em_unit();

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : from_u8(opt_def->tooltip);

    // Vertical layout: label on top, text control below
    auto *container_sizer = new wxBoxSizer(wxVERTICAL);

    // Header row with icons and label
    auto *header_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    auto *lock_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("lock_closed"));
    lock_icon->SetMinSize(GetScaledIconSizeWx());
    lock_icon->SetBackgroundColour(bg_color);
    lock_icon->SetToolTip(_L("Value is same as in the system preset"));
    header_sizer->Add(lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    auto *undo_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("dot"));
    undo_icon->SetMinSize(GetScaledIconSizeWx());
    undo_icon->SetBackgroundColour(bg_color);
    header_sizer->Add(undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    wxString label_with_colon = label + ":";
    auto *label_text = new wxStaticText(parent, wxID_ANY, label_with_colon);
    label_text->SetBackgroundColour(bg_color);
    if (!tooltip.empty())
        label_text->SetToolTip(tooltip);
    header_sizer->Add(label_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

    container_sizer->Add(header_sizer, 0, wxEXPAND);

    // Multi-line text control - full width
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    std::string original_value;

    int text_height = num_lines * em * 1.5;
    auto *text = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, text_height),
                                wxTE_MULTILINE | wxHSCROLL | wxBORDER_SIMPLE);

    if (config.has(opt_key))
    {
        wxString value_str = from_u8(config.opt_serialize(opt_key));
        text->SetValue(value_str);
        original_value = config.opt_serialize(opt_key);
    }
    if (!tooltip.empty())
        text->SetToolTip(tooltip);

    text->Bind(wxEVT_KILL_FOCUS,
               [this, opt_key](wxFocusEvent &evt)
               {
                   OnSettingChanged(opt_key);
                   evt.Skip();
               });

    container_sizer->Add(text, 0, wxEXPAND | wxTOP, em / 4);

    // Store UI elements
    SettingUIElements ui_elem;
    ui_elem.control = text;
    ui_elem.lock_icon = lock_icon;
    ui_elem.undo_icon = undo_icon;
    ui_elem.label_text = label_text;
    ui_elem.original_value = original_value;
    m_setting_controls[opt_key] = ui_elem;

    UpdateUndoUI(opt_key);

    // Bind undo icon click
    undo_icon->Bind(wxEVT_LEFT_DOWN,
                    [this, opt_key](wxMouseEvent &)
                    {
                        auto it = m_setting_controls.find(opt_key);
                        if (it == m_setting_controls.end())
                            return;

                        if (auto *txt = dynamic_cast<wxTextCtrl *>(it->second.control))
                        {
                            txt->SetValue(from_u8(it->second.original_value));
                        }

                        OnSettingChanged(opt_key);
                        UpdateUndoUI(opt_key);
                    });

    sizer->Add(container_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
}

void FilamentSettingsPanel::CreateNullableSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                                     const wxString &label)
{
    // Check sidebar visibility - skip if user has hidden this setting
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        return;

    int em = wxGetApp().em_unit();

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : from_u8(opt_def->tooltip);

    // Left side sizer: icons + checkbox + label
    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    // Lock and undo icons first (same order as regular settings)
    auto *lock_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("lock_closed"));
    lock_icon->SetMinSize(GetScaledIconSizeWx());
    lock_icon->SetBackgroundColour(bg_color);
    lock_icon->SetToolTip(_L("Value is same as in the system preset"));
    left_sizer->Add(lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    auto *undo_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("dot"));
    undo_icon->SetMinSize(GetScaledIconSizeWx());
    undo_icon->SetBackgroundColour(bg_color);
    left_sizer->Add(undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    // Enable checkbox after icons (the key difference from CreateSettingRow)
    auto *enable_checkbox = new ::CheckBox(parent);
    enable_checkbox->SetBackgroundColour(SidebarColors::Background());
    enable_checkbox->SetToolTip(_L("Check to override printer settings"));
    left_sizer->Add(enable_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    // Store checkbox in map for quick access
    m_override_checkboxes[opt_key] = enable_checkbox;

    // Label
    wxString label_with_colon = label + ":";
    auto *label_text = new wxStaticText(parent, wxID_ANY, label_with_colon, wxDefaultPosition, wxDefaultSize,
                                        wxST_ELLIPSIZE_END);
    label_text->SetMinSize(wxSize(1, -1));
    label_text->SetBackgroundColour(bg_color);
    if (!tooltip.empty())
        label_text->SetToolTip(tooltip);
    left_sizer->Add(label_text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

    row_sizer->Add(left_sizer, 1, wxEXPAND);

    wxWindow *value_ctrl = nullptr;
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    std::string original_value;
    std::string last_meaningful_value;

    // Check if option is nil (unchecked state)
    bool is_nil = false;
    if (config.has(opt_key))
    {
        const ConfigOption *opt = config.option(opt_key);
        is_nil = opt->is_nil();
    }

    switch (opt_def->type)
    {
    case coBools:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *checkbox = new ::CheckBox(parent);
        checkbox->SetBackgroundColour(SidebarColors::Background());

        if (config.has(opt_key))
        {
            auto *opt = config.option<ConfigOptionBoolsNullable>(opt_key);
            if (opt && !opt->values.empty())
            {
                is_nil = (opt->values[0] == ConfigOptionBoolsNullable::nil_value());
                bool val = is_nil ? false : (opt->values[0] != 0);
                checkbox->SetValue(val);
                // Always serialize to get correct original_value for comparison (nil serializes to "nil")
                original_value = config.opt_serialize(opt_key);
                // For nil values, default to false; otherwise use actual value
                last_meaningful_value = is_nil ? "0" : (val ? "1" : "0");
            }
        }
        if (!tooltip.empty())
            checkbox->SetToolTip(tooltip);

        checkbox->Bind(wxEVT_CHECKBOX, [this, opt_key](wxCommandEvent &) { OnSettingChanged(opt_key); });

        value_sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
        value_sizer->AddStretchSpacer(1);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = checkbox;
        break;
    }

    case coFloats:
    case coPercents:
    case coFloatsOrPercents:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition,
                                     wxSize(GetScaledInputWidth(), -1));
        wxGetApp().UpdateDarkUI(text);

        if (config.has(opt_key))
        {
            const ConfigOption *opt = config.option(opt_key);
            is_nil = opt->is_nil();
            // Always serialize to get correct original_value for comparison (nil serializes to "nil")
            original_value = config.opt_serialize(opt_key);
            if (!is_nil)
            {
                wxString value_str = from_u8(original_value);
                text->SetValue(value_str);
                last_meaningful_value = original_value;
            }
            else
            {
                text->SetValue(_L("N/A"));
                // Try to get a meaningful default
                last_meaningful_value = "0";
            }
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL); // Fixed 60px width

        // Add sidetext if available
        if (!opt_def->sidetext.empty())
        {
            std::string sidetext = opt_def->sidetext;
            size_t paren_pos = sidetext.find('(');
            if (paren_pos != std::string::npos)
                sidetext = sidetext.substr(0, paren_pos);
            boost::trim(sidetext);
            if (!sidetext.empty())
            {
                auto *unit_text = new wxStaticText(parent, wxID_ANY, from_u8(sidetext));
                value_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            }
        }

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }

    case coInts:
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition,
                                     wxSize(GetScaledInputWidth(), -1));
        wxGetApp().UpdateDarkUI(text);

        if (config.has(opt_key))
        {
            const ConfigOption *opt = config.option(opt_key);
            is_nil = opt->is_nil();
            // Always serialize to get correct original_value for comparison (nil serializes to "nil")
            original_value = config.opt_serialize(opt_key);
            if (!is_nil)
            {
                wxString value_str = from_u8(original_value);
                text->SetValue(value_str);
                last_meaningful_value = original_value;
            }
            else
            {
                text->SetValue(_L("N/A"));
                last_meaningful_value = "0";
            }
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL); // Fixed 60px width

        // Add sidetext (units) if available - strip parenthetical notes for compact display
        if (!opt_def->sidetext.empty())
        {
            std::string sidetext = opt_def->sidetext;
            size_t paren_pos = sidetext.find('(');
            if (paren_pos != std::string::npos)
                sidetext = sidetext.substr(0, paren_pos);
            boost::trim(sidetext);
            if (!sidetext.empty())
            {
                auto *unit_text = new wxStaticText(parent, wxID_ANY, from_u8(sidetext));
                value_sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
            }
        }

        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }

    default:
    {
        // Generic text handling
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, wxDefaultSize);
        wxGetApp().UpdateDarkUI(text);
        text->SetMinSize(wxSize(1, -1));

        if (config.has(opt_key))
        {
            const ConfigOption *opt = config.option(opt_key);
            is_nil = opt->is_nil();
            // Always serialize to get correct original_value for comparison (nil serializes to "nil")
            original_value = config.opt_serialize(opt_key);
            if (!is_nil)
            {
                wxString value_str = from_u8(original_value);
                text->SetValue(value_str);
                last_meaningful_value = original_value;
            }
            else
            {
                text->SetValue(_L("N/A"));
                last_meaningful_value = "";
            }
        }
        if (!tooltip.empty())
            text->SetToolTip(tooltip);

        text->Bind(wxEVT_KILL_FOCUS,
                   [this, opt_key](wxFocusEvent &evt)
                   {
                       OnSettingChanged(opt_key);
                       evt.Skip();
                   });

        value_sizer->Add(text, 1, wxEXPAND);
        row_sizer->Add(value_sizer, 1, wxEXPAND);
        value_ctrl = text;
        break;
    }
    }

    // Set initial checkbox and control state
    enable_checkbox->SetValue(!is_nil);
    if (value_ctrl)
    {
        // Use SetEditable instead of Enable for wxTextCtrl on Windows (Enable ignores SetBackgroundColour)
        if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(value_ctrl))
        {
#ifdef _WIN32
            text->SetEditable(!is_nil);
            if (!is_nil)
            {
                text->SetBackgroundColour(SidebarColors::InputBackground());
                text->SetForegroundColour(SidebarColors::InputForeground());
            }
            else
            {
                text->SetBackgroundColour(SidebarColors::DisabledBackground());
                text->SetForegroundColour(SidebarColors::DisabledForeground());
            }
            text->Refresh();
#else
            text->Enable(!is_nil);
#endif
        }
        else
        {
            value_ctrl->Enable(!is_nil);
        }
    }

    // Store UI elements
    if (value_ctrl)
    {
        SettingUIElements ui_elem;
        ui_elem.control = value_ctrl;
        ui_elem.lock_icon = lock_icon;
        ui_elem.undo_icon = undo_icon;
        ui_elem.label_text = label_text;
        ui_elem.enable_checkbox = enable_checkbox;
        ui_elem.original_value = original_value;
        ui_elem.last_meaningful_value = last_meaningful_value;
        m_setting_controls[opt_key] = ui_elem;

        UpdateUndoUI(opt_key);

        // Bind undo icon click - nullable settings need special handling for nil state
        undo_icon->Bind(wxEVT_LEFT_DOWN,
                        [this, opt_key, enable_checkbox](wxMouseEvent &)
                        {
                            auto it = m_setting_controls.find(opt_key);
                            if (it == m_setting_controls.end())
                                return;

                            const std::string &original = it->second.original_value;
                            bool original_was_nil = (original == "nil" || original.empty());

                            if (original_was_nil)
                            {
                                // Original was nil - trigger unchecked state via OnNullableSettingChanged
                                enable_checkbox->SetValue(false);
                                OnNullableSettingChanged(opt_key, false);
                            }
                            else
                            {
                                // Original was a real value - enable and restore
                                enable_checkbox->SetValue(true);

                                // Restore the value to control
                                if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
                                {
                                    text->SetValue(from_u8(original));
                                }
                                else if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
                                {
                                    cb->SetValue(original == "1");
                                }
                                else if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
                                {
                                    text_input->SetValue(from_u8(original));
                                }

                                OnNullableSettingChanged(opt_key, true);
                            }

                            UpdateUndoUI(opt_key);
                        });
    }

    // Bind enable checkbox
    enable_checkbox->Bind(wxEVT_CHECKBOX,
                          [this, opt_key](wxCommandEvent &evt) { OnNullableSettingChanged(opt_key, evt.IsChecked()); });

    sizer->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
}

void FilamentSettingsPanel::OnNullableSettingChanged(const std::string &opt_key, bool is_checked)
{
    if (m_disable_update)
        return;

    m_disable_update = true;

    auto it = m_setting_controls.find(opt_key);
    if (it == m_setting_controls.end())
    {
        m_disable_update = false;
        return;
    }

    DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;

    if (is_checked)
    {
        // Enable the field and restore last meaningful value
        if (it->second.control)
        {
            // Use SetEditable instead of Enable for wxTextCtrl on Windows (Enable ignores SetBackgroundColour)
            if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(it->second.control))
            {
#ifdef _WIN32
                text->SetEditable(true);
                text->SetBackgroundColour(SidebarColors::InputBackground());
                text->SetForegroundColour(SidebarColors::InputForeground());
                text->Refresh();
#else
                text->Enable(true);
                wxGetApp().UpdateDarkUI(text);
                text->Refresh();
#endif
            }
            else
            {
                it->second.control->Enable(true);
            }
        }

        // Restore last meaningful value or use default
        std::string value_to_set = it->second.last_meaningful_value;
        if (value_to_set.empty())
            value_to_set = "0";

        // Set the value on the control
        if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            text->SetValue(from_u8(value_to_set));
        }
        else if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
        {
            cb->SetValue(value_to_set == "1");
        }

        // Set in config (non-nil)
        config.set_deserialize_strict(opt_key, value_to_set);
    }
    else
    {
        // Disable the field and set to N/A
        if (it->second.control)
        {
            // Use SetEditable instead of Enable for wxTextCtrl on Windows (Enable ignores SetBackgroundColour)
            if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(it->second.control))
            {
#ifdef _WIN32
                text->SetEditable(false);
                text->SetBackgroundColour(SidebarColors::DisabledBackground());
                text->SetForegroundColour(SidebarColors::DisabledForeground());
                text->Refresh();
#else
                text->Enable(false);
#endif
            }
            else
            {
                it->second.control->Enable(false);
            }
        }

        // Store current value as last meaningful before setting to nil
        if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            std::string current = into_u8(text->GetValue());
            if (current != into_u8(_L("N/A")) && !current.empty())
                it->second.last_meaningful_value = current;
            text->SetValue(_L("N/A"));
        }
        else if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
        {
            it->second.last_meaningful_value = cb->GetValue() ? "1" : "0";
        }

        // Set config option to nil
        ConfigOption *opt = config.option(opt_key, true);
        if (opt)
        {
            // Setting the entire option to its nil state
            // For nullable options, we need to set the nil value
            const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
            if (opt_def)
            {
                switch (opt_def->type)
                {
                case coBools:
                    if (auto *bools_opt = dynamic_cast<ConfigOptionBoolsNullable *>(opt))
                    {
                        if (!bools_opt->values.empty())
                            bools_opt->values[0] = ConfigOptionBoolsNullable::nil_value();
                    }
                    break;
                case coFloats:
                    if (auto *floats_opt = dynamic_cast<ConfigOptionFloatsNullable *>(opt))
                    {
                        if (!floats_opt->values.empty())
                            floats_opt->values[0] = ConfigOptionFloatsNullable::nil_value();
                    }
                    break;
                case coPercents:
                    if (auto *pcts_opt = dynamic_cast<ConfigOptionPercentsNullable *>(opt))
                    {
                        if (!pcts_opt->values.empty())
                            pcts_opt->values[0] = ConfigOptionPercentsNullable::nil_value();
                    }
                    break;
                case coFloatsOrPercents:
                    if (auto *fop_opt = dynamic_cast<ConfigOptionFloatsOrPercentsNullable *>(opt))
                    {
                        if (!fop_opt->values.empty())
                            fop_opt->values[0] = ConfigOptionFloatsOrPercentsNullable::nil_value();
                    }
                    break;
                case coInts:
                    if (auto *ints_opt = dynamic_cast<ConfigOptionIntsNullable *>(opt))
                    {
                        if (!ints_opt->values.empty())
                            ints_opt->values[0] = ConfigOptionIntsNullable::nil_value();
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

    // Mark dirty and sync
    wxGetApp().preset_bundle->filaments.get_edited_preset().set_dirty(true);

    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT))
    {
        tab->reload_config();
        tab->update_dirty();
        tab->update_changed_ui();
    }

    if (GetPlater())
        GetPlater()->on_config_change(config);

    // Update toggle states for dependent options
    UpdateOverridesToggleState();

    // Update undo icon state
    UpdateUndoUI(opt_key);

    m_disable_update = false;
}

void FilamentSettingsPanel::UpdateOverridesToggleState()
{
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;

    // Travel lift dependencies
    // filament_travel_ramping_lift controls: travel_max_lift, travel_slope, travel_lift_before_obstacle
    // When ramping is enabled, retract_lift is disabled
    bool uses_ramping_lift = false;
    {
        auto *opt = config.option<ConfigOptionBoolsNullable>("filament_travel_ramping_lift");
        if (opt && !opt->values.empty())
        {
            uses_ramping_lift = !opt->is_nil() && opt->values[0] != 0;
        }
    }

    // Check if lifting is happening (either through ramping or fixed lift)
    // For overrides, nil means "not overriding" - dependent fields should be disabled
    // Use OR logic: lifting happens if either max_lift > 0 OR retract_lift > 0
    bool is_lifting = false;
    {
        auto *max_lift_opt = config.option<ConfigOptionFloatsNullable>("filament_travel_max_lift");
        auto *retract_lift_opt = config.option<ConfigOptionFloatsNullable>("filament_retract_lift");

        bool has_max_lift = (max_lift_opt && !max_lift_opt->is_nil() && !max_lift_opt->values.empty() &&
                             max_lift_opt->values[0] > 0);
        bool has_retract_lift = (retract_lift_opt && !retract_lift_opt->is_nil() && !retract_lift_opt->values.empty() &&
                                 retract_lift_opt->values[0] > 0);

        is_lifting = has_max_lift || has_retract_lift;
    }

    // Apply travel lift toggle logic
    auto toggle_control = [this](const std::string &key, bool enable)
    {
        auto it = m_setting_controls.find(key);
        if (it == m_setting_controls.end())
            return;

        auto cb_it = m_override_checkboxes.find(key);
        if (cb_it == m_override_checkboxes.end() || !cb_it->second)
            return;

        ::CheckBox *checkbox = cb_it->second;

        if (!enable)
        {
            // When disabled by toggle logic: uncheck and disable checkbox, disable control
            checkbox->SetValue(false);
            checkbox->Enable(false);
            if (it->second.control)
                it->second.control->Enable(false);
        }
        else
        {
            // When enabled by toggle logic: enable checkbox, control state depends on checkbox
            checkbox->Enable(true);
            if (it->second.control)
                it->second.control->Enable(checkbox->GetValue());
        }
    };

    // Ramping lift disables fixed lift, enables ramping options
    toggle_control("filament_retract_lift", !uses_ramping_lift);
    toggle_control("filament_travel_max_lift", uses_ramping_lift);
    toggle_control("filament_travel_slope", uses_ramping_lift);
    toggle_control("filament_travel_lift_before_obstacle", uses_ramping_lift);

    // Only lift above/below depend on is_lifting
    toggle_control("filament_retract_lift_above", is_lifting);
    toggle_control("filament_retract_lift_below", is_lifting);

    // Retraction dependencies
    // filament_retract_length > 0 enables all other retraction options
    // For overrides, nil means "not overriding" - dependent fields should be disabled
    bool have_retract_length = false;
    {
        auto *opt = config.option<ConfigOptionFloatsNullable>("filament_retract_length");
        if (opt && !opt->values.empty())
        {
            have_retract_length = !opt->is_nil() && opt->values[0] > 0;
        }
    }

    toggle_control("filament_retract_speed", have_retract_length);
    toggle_control("filament_deretract_speed", have_retract_length);
    toggle_control("filament_retract_restart_extra", have_retract_length);
    toggle_control("filament_retract_before_travel", have_retract_length);
    toggle_control("filament_retract_layer_change", have_retract_length);
    toggle_control("filament_wipe", have_retract_length);
    toggle_control("filament_wipe_extend", have_retract_length);
    toggle_control("filament_retract_before_wipe", have_retract_length);
    toggle_control("filament_wipe_length", have_retract_length);
}

void FilamentSettingsPanel::OnSettingChanged(const std::string &opt_key)
{
    if (m_disable_update)
        return;

    m_disable_update = true;

    auto it = m_setting_controls.find(opt_key);
    if (it == m_setting_controls.end())
    {
        m_disable_update = false;
        return;
    }

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
    {
        m_disable_update = false;
        return;
    }

    DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;

    switch (opt_def->type)
    {
    case coBool:
        if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
        {
            config.set_key_value(opt_key, new ConfigOptionBool(cb->GetValue()));
        }
        break;
    case coBools:
        if (auto *cb = dynamic_cast<::CheckBox *>(it->second.control))
        {
            // For coBools, we set a single value that applies to extruder 0
            auto *opt = config.option<ConfigOptionBools>(opt_key, true);
            if (opt && !opt->values.empty())
                opt->values[0] = cb->GetValue();
        }
        break;
    case coInt:
        if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
        {
            config.set_key_value(opt_key, new ConfigOptionInt(spin->GetValue()));
        }
        break;
    case coInts:
        if (auto *spin = dynamic_cast<SpinInput *>(it->second.control))
        {
            auto *opt = config.option<ConfigOptionInts>(opt_key, true);
            if (opt && !opt->values.empty())
                opt->values[0] = spin->GetValue();
        }
        break;
    case coEnum:
        if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
        {
            int sel = combo->GetSelection();
            if (sel != wxNOT_FOUND && opt_def->enum_def && opt_def->enum_def->has_values())
            {
                const auto &values = opt_def->enum_def->values();
                if (sel < static_cast<int>(values.size()))
                {
                    config.set_deserialize_strict(opt_key, values[sel]);
                }
            }
        }
        break;
    case coEnums:
        // For vector enums (e.g. cooling_slowdown_logic, fan_spinup_response_type),
        // set only the first extruder value to preserve other extruder values.
        if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
        {
            int sel = combo->GetSelection();
            if (sel != wxNOT_FOUND && opt_def->enum_def && opt_def->enum_def->has_values())
            {
                int enum_val = opt_def->enum_def->index_to_enum(sel);
                auto *opt = dynamic_cast<ConfigOptionEnumsGeneric *>(config.optptr(opt_key, true));
                if (opt && !opt->values.empty())
                    opt->values[0] = enum_val;
            }
        }
        break;
    case coFloat:
    case coFloats:
    case coFloatOrPercent:
    case coFloatsOrPercents:
    case coPercent:
    case coPercents:
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            std::string value_str = into_u8(text_input->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            std::string value_str = into_u8(text->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        break;
    case coString:
    case coStrings:
    default:
        // Special handling for filament_colour - get color from panel
        if (opt_key == "filament_colour")
        {
            if (auto *panel = dynamic_cast<wxPanel *>(it->second.control))
            {
                wxColour color = panel->GetBackgroundColour();
                wxString color_str = wxString::Format("#%02X%02X%02X", color.Red(), color.Green(), color.Blue());
                auto *opt = config.option<ConfigOptionStrings>(opt_key, true);
                if (opt && !opt->values.empty())
                    opt->values[0] = into_u8(color_str);
            }
        }
        else if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
        {
            // select_open string: get value from dropdown selection or typed text
            int sel = combo->GetSelection();
            if (sel != wxNOT_FOUND && opt_def->enum_def && opt_def->enum_def->has_values() &&
                sel < static_cast<int>(opt_def->enum_def->values().size()))
            {
                config.set_deserialize_strict(opt_key, opt_def->enum_def->values()[sel]);
            }
            else
            {
                // User typed a custom value
                config.set_deserialize_strict(opt_key, into_u8(combo->GetValue()));
            }
        }
        else if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            std::string value_str = into_u8(text_input->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            std::string value_str = into_u8(text->GetValue());
            config.set_deserialize_strict(opt_key, value_str);
        }
        break;
    }

    UpdateUndoUI(opt_key);

    // Mutual exclusion: manual fan speeds and dynamic fan speeds cannot both be enabled
    // Mirrors TabFilament::on_value_change() behavior
    if (opt_key == "enable_manual_fan_speeds" && config.has("enable_dynamic_fan_speeds"))
    {
        bool manual_enabled = config.opt_bool("enable_manual_fan_speeds", 0);
        if (manual_enabled && config.opt_bool("enable_dynamic_fan_speeds", 0))
        {
            // Disable dynamic fan speeds
            auto *opt = config.option<ConfigOptionBools>("enable_dynamic_fan_speeds", true);
            if (opt && !opt->values.empty())
                opt->values[0] = false;

            // Update the UI checkbox
            auto dynamic_it = m_setting_controls.find("enable_dynamic_fan_speeds");
            if (dynamic_it != m_setting_controls.end())
            {
                if (auto *cb = dynamic_cast<::CheckBox *>(dynamic_it->second.control))
                    cb->SetValue(false);
                UpdateUndoUI("enable_dynamic_fan_speeds");
            }
        }
    }
    else if (opt_key == "enable_dynamic_fan_speeds" && config.has("enable_manual_fan_speeds"))
    {
        bool dynamic_enabled = config.opt_bool("enable_dynamic_fan_speeds", 0);
        if (dynamic_enabled && config.opt_bool("enable_manual_fan_speeds", 0))
        {
            // Disable manual fan speeds
            auto *opt = config.option<ConfigOptionBools>("enable_manual_fan_speeds", true);
            if (opt && !opt->values.empty())
                opt->values[0] = false;

            // Update the UI checkbox
            auto manual_it = m_setting_controls.find("enable_manual_fan_speeds");
            if (manual_it != m_setting_controls.end())
            {
                if (auto *cb = dynamic_cast<::CheckBox *>(manual_it->second.control))
                    cb->SetValue(false);
                UpdateUndoUI("enable_manual_fan_speeds");
            }
        }
    }

    // Update SlicedInfo when spool weight changes - mirrors TabFilament::on_value_change() behavior
    if (opt_key == "filament_spool_weight")
    {
        wxGetApp().sidebar().update_sliced_info_sizer();
    }

    wxGetApp().preset_bundle->filaments.get_edited_preset().set_dirty(true);

    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT))
    {
        // The sidebar and tab share the same DynamicPrintConfig object, so
        // load_config() would find no diff. Force the tab to re-read its UI
        // fields from the config and update dirty/undo state.
        tab->reload_config();
        tab->update_dirty();
        tab->update_changed_ui();
    }

    if (GetPlater())
    {
        GetPlater()->on_config_change(config);
    }

    // Apply toggle logic to enable/disable dependent options
    ApplyToggleLogic();

    m_disable_update = false;
}

void FilamentSettingsPanel::UpdateUndoUI(const std::string &opt_key)
{
    auto it = m_setting_controls.find(opt_key);
    if (it != m_setting_controls.end())
        UpdateUndoUICommon(opt_key, it->second.undo_icon, it->second.lock_icon, it->second.original_value);
}

void FilamentSettingsPanel::RefreshFromConfig()
{
    m_disable_update = true;

    const DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;

    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
        if (!opt_def || !config.has(opt_key))
            continue;

        const ConfigOption *opt = config.option(opt_key);
        bool is_nil = opt->is_nil();

        // Handle nullable options with enable checkbox
        if (ui_elem.enable_checkbox)
        {
            ui_elem.enable_checkbox->SetValue(!is_nil);
            if (ui_elem.control)
            {
                // Use SetEditable instead of Enable for wxTextCtrl on Windows (Enable ignores SetBackgroundColour)
                if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
                {
#ifdef _WIN32
                    text->SetEditable(!is_nil);
                    if (!is_nil)
                    {
                        text->SetBackgroundColour(SidebarColors::InputBackground());
                        text->SetForegroundColour(SidebarColors::InputForeground());
                    }
                    else
                    {
                        text->SetBackgroundColour(SidebarColors::DisabledBackground());
                        text->SetForegroundColour(SidebarColors::DisabledForeground());
                    }
                    text->Refresh();
#else
                    text->Enable(!is_nil);
#endif
                }
                else
                {
                    ui_elem.control->Enable(!is_nil);
                }
            }

            if (is_nil)
            {
                // Set display to N/A
                if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
                {
                    text->SetValue(_L("N/A"));
                }
                // Note: Do NOT update original_value here
                continue;
            }
        }

        // Note: Do NOT update original_value here - it should only be set when
        // the control is created or when a preset is loaded/saved
        if (!is_nil)
            ui_elem.last_meaningful_value = config.opt_serialize(opt_key);

        switch (opt_def->type)
        {
        case coBool:
            if (auto *cb = dynamic_cast<::CheckBox *>(ui_elem.control))
            {
                cb->SetValue(config.opt_bool(opt_key));
            }
            break;
        case coBools:
            if (auto *cb = dynamic_cast<::CheckBox *>(ui_elem.control))
            {
                if (opt_def->nullable)
                {
                    auto *bools_opt = config.option<ConfigOptionBoolsNullable>(opt_key);
                    if (bools_opt && !bools_opt->values.empty() && !is_nil)
                        cb->SetValue(bools_opt->values[0] != 0);
                }
                else
                {
                    auto *bools_opt = config.option<ConfigOptionBools>(opt_key);
                    if (bools_opt && !bools_opt->values.empty())
                        cb->SetValue(bools_opt->values[0]);
                }
            }
            break;
        case coInt:
            if (auto *spin = dynamic_cast<SpinInput *>(ui_elem.control))
            {
                spin->SetValue(config.opt_int(opt_key));
            }
            else if (auto *text_input = dynamic_cast<::TextInput *>(ui_elem.control))
            {
                text_input->SetValue(from_u8(config.opt_serialize(opt_key)));
            }
            else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
            {
                text->SetValue(from_u8(config.opt_serialize(opt_key)));
            }
            break;
        case coInts:
            if (auto *spin = dynamic_cast<SpinInput *>(ui_elem.control))
            {
                auto *ints_opt = config.option<ConfigOptionInts>(opt_key);
                if (ints_opt && !ints_opt->values.empty())
                    spin->SetValue(ints_opt->values[0]);
            }
            else if (auto *text_input = dynamic_cast<::TextInput *>(ui_elem.control))
            {
                text_input->SetValue(from_u8(config.opt_serialize(opt_key)));
            }
            else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
            {
                text->SetValue(from_u8(config.opt_serialize(opt_key)));
            }
            break;
        case coEnum:
            if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            {
                if (opt_def->enum_def && opt_def->enum_def->has_values())
                {
                    std::string current = config.opt_serialize(opt_key);
                    const auto &values = opt_def->enum_def->values();
                    for (size_t idx = 0; idx < values.size(); ++idx)
                    {
                        if (values[idx] == current)
                        {
                            combo->SetSelection(static_cast<int>(idx));
                            break;
                        }
                    }
                }
            }
            break;
        case coEnums:
            // For vector enums, extract only the first extruder's value for display.
            // opt_serialize() returns all values comma-separated which won't match a single dropdown item.
            if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            {
                if (opt_def->enum_def && opt_def->enum_def->has_values())
                {
                    const ConfigOption *raw_opt = config.option(opt_key);
                    if (raw_opt)
                    {
                        auto *vec_opt = dynamic_cast<const ConfigOptionVectorBase *>(raw_opt);
                        if (vec_opt && vec_opt->size() > 0)
                        {
                            std::vector<std::string> serialized = vec_opt->vserialize();
                            if (!serialized.empty())
                            {
                                const auto &values = opt_def->enum_def->values();
                                for (size_t idx = 0; idx < values.size(); ++idx)
                                {
                                    if (values[idx] == serialized[0])
                                    {
                                        combo->SetSelection(static_cast<int>(idx));
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            break;
        default:
            // Special handling for filament_colour - update color panel
            if (opt_key == "filament_colour")
            {
                if (auto *panel = dynamic_cast<wxPanel *>(ui_elem.control))
                {
                    auto *opt = config.option<ConfigOptionStrings>(opt_key);
                    if (opt && !opt->values.empty() && !opt->values[0].empty())
                    {
                        wxColour clr(from_u8(opt->values[0]));
                        if (clr.IsOk())
                        {
                            panel->SetBackgroundColour(clr);
                            panel->Refresh();
                        }
                    }
                }
            }
            else if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            {
                // select_open string: update dropdown selection or set custom text
                std::string current = config.opt_serialize(opt_key);
                bool found = false;
                if (opt_def->enum_def && opt_def->enum_def->has_values())
                {
                    const auto &values = opt_def->enum_def->values();
                    for (size_t idx = 0; idx < values.size(); ++idx)
                    {
                        if (values[idx] == current)
                        {
                            combo->SetSelection(static_cast<int>(idx));
                            found = true;
                            break;
                        }
                    }
                }
                if (!found)
                    combo->SetValue(from_u8(current));
            }
            else if (auto *text_input = dynamic_cast<::TextInput *>(ui_elem.control))
            {
                text_input->SetValue(from_u8(config.opt_serialize(opt_key)));
            }
            else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
            {
                text->SetValue(from_u8(config.opt_serialize(opt_key)));
            }
            break;
        }

        UpdateUndoUI(opt_key);
    }

    // Update toggle states for nullable override options
    UpdateOverridesToggleState();

    // Apply toggle logic to enable/disable dependent options
    ApplyToggleLogic();

    // UNIFIED THEMING: Call SysColorsChanged on parent controls (TextInput, SpinInput, ComboBox)
    // These controls contain ThemedTextCtrl and handle their own color management via WM_CTLCOLOREDIT
    for (auto &[key, ui_elem] : m_setting_controls)
    {
        // Check for our custom controls first - they handle their own theming
        if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *text = dynamic_cast<wxTextCtrl *>(ui_elem.control))
        {
            // Plain wxTextCtrl (not inside our custom controls) - apply colors directly
#ifdef _WIN32
            bool is_editable = text->IsEditable();
            // Disable visual styles so SetBackgroundColour works properly
            SetWindowTheme(text->GetHWND(), L"", L"");
            text->SetBackgroundColour(is_editable ? SidebarColors::InputBackground()
                                                  : SidebarColors::DisabledBackground());
            text->SetForegroundColour(is_editable ? SidebarColors::InputForeground()
                                                  : SidebarColors::DisabledForeground());
            RedrawWindow(text->GetHWND(), NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
#endif
        }
    }

    m_disable_update = false;
}

void FilamentSettingsPanel::ToggleOption(const std::string &opt_key, bool enable)
{
    auto it = m_setting_controls.find(opt_key);
    if (it != m_setting_controls.end())
        ToggleOptionControl(it->second.control, enable);
}

void FilamentSettingsPanel::ApplyToggleLogic()
{
    // Get current config - mirrors TabFilament::toggle_options()
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;

    // Cooling dependencies
    bool cooling = config.opt_bool("cooling", 0);
    bool fan_always_on = cooling || config.opt_bool("fan_always_on", 0);

    for (const char *el : {"max_fan_speed", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed",
                           "cooling_slowdown_logic"})
        ToggleOption(el, cooling);

    for (const char *el : {"min_fan_speed", "full_fan_speed_layer"})
        ToggleOption(el, fan_always_on);

    // Manual fan controls require enable_manual_fan_speeds to be ON
    bool manual_fan_enabled = config.opt_bool("enable_manual_fan_speeds", 0);
    for (const char *el : {"manual_fan_speed_perimeter", "manual_fan_speed_external_perimeter",
                           "manual_fan_speed_interlocking_perimeter", "manual_fan_speed_internal_infill",
                           "manual_fan_speed_solid_infill", "manual_fan_speed_top_solid_infill",
                           "manual_fan_speed_ironing", "manual_fan_speed_gap_fill", "manual_fan_speed_skirt",
                           "manual_fan_speed_support_material", "manual_fan_speed_support_interface"})
        ToggleOption(el, manual_fan_enabled);

    // Dynamic fan speed sub-options disabled when manual fan is enabled
    bool dynamic_fan_speeds = config.opt_bool("enable_dynamic_fan_speeds", 0);
    for (int i = 0; i < 4; i++)
    {
        ToggleOption("overhang_fan_speed_" + std::to_string(i), !manual_fan_enabled && dynamic_fan_speeds);
    }

    // Cooling perimeter transition distance
    auto slowdown_logic_opt = config.option("cooling_slowdown_logic");
    bool cooling_preserve_perimeters = cooling && slowdown_logic_opt &&
                                       static_cast<CoolingSlowdownLogicType>(slowdown_logic_opt->getInts().at(0)) ==
                                           CoolingSlowdownLogicType::ConsistentSurface;
    ToggleOption("cooling_perimeter_transition_distance", cooling_preserve_perimeters);

    // Multitool ramming dependencies
    bool multitool_ramming = config.opt_bool("filament_multitool_ramming", 0);
    ToggleOption("filament_multitool_ramming_volume", multitool_ramming);
    ToggleOption("filament_multitool_ramming_flow", multitool_ramming);
}

void FilamentSettingsPanel::msw_rescale()
{
    // Update icon sizes and rescale controls for DPI scaling
    wxSize icon_size = GetScaledIconSizeWx();
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (ui_elem.lock_icon)
            ui_elem.lock_icon->SetMinSize(icon_size);
        if (ui_elem.undo_icon)
            ui_elem.undo_icon->SetMinSize(icon_size);
        // Rescale SpinInput controls so internal buttons reposition correctly
        if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->Rescale();
    }

    TabbedSettingsPanel::msw_rescale();
}

void FilamentSettingsPanel::sys_color_changed()
{
    TabbedSettingsPanel::sys_color_changed();

    // Get current theme background color
    wxColour bg_color = SidebarColors::Background();

    // Refresh ALL setting controls for the new theme
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (!ui_elem.control)
            continue;

        // Update label background color
        if (ui_elem.label_text)
            ui_elem.label_text->SetBackgroundColour(bg_color);

        // Update icon background colors
        if (ui_elem.lock_icon)
            ui_elem.lock_icon->SetBackgroundColour(bg_color);
        if (ui_elem.undo_icon)
            ui_elem.undo_icon->SetBackgroundColour(bg_color);

        // Handle all custom widget types that have SysColorsChanged/sys_color_changed
        if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *checkbox = dynamic_cast<::CheckBox *>(ui_elem.control))
            checkbox->sys_color_changed();

        UpdateUndoUI(opt_key);
    }

    // Update all ScalableButtons
    UpdateScalableButtonsRecursive(this);
}

void FilamentSettingsPanel::OnSysColorChanged()
{
    // Update all setting controls - call SysColorsChanged() on each custom widget
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (!ui_elem.control)
            continue;

        // Try each custom widget type that has SysColorsChanged/sys_color_changed
        if (auto *text_input = dynamic_cast<TextInput *>(ui_elem.control))
            text_input->SysColorsChanged();
        else if (auto *spin = dynamic_cast<SpinInputBase *>(ui_elem.control))
            spin->SysColorsChanged();
        else if (auto *combo = dynamic_cast<::ComboBox *>(ui_elem.control))
            combo->SysColorsChanged();
        else if (auto *checkbox = dynamic_cast<::CheckBox *>(ui_elem.control))
            checkbox->sys_color_changed();
    }
}

// ============================================================================
// ProcessSection Implementation - Now wraps PrintSettingsPanel
// ============================================================================

ProcessSection::ProcessSection(wxWindow *parent, Plater *plater)
    : wxPanel(parent, wxID_ANY)
    , m_plater(plater)
    , m_preset_combo(nullptr)
    , m_settings_panel(nullptr)
    , m_btn_save(nullptr)
{
    BuildUI();
}

void ProcessSection::BuildUI()
{
    int em = wxGetApp().em_unit();

    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // Settings panel with nested accordions for each category
    m_settings_panel = new PrintSettingsPanel(this, m_plater);
    m_main_sizer->Add(m_settings_panel, 1, wxEXPAND);

    SetSizer(m_main_sizer);
}

void ProcessSection::SetPresetComboBox(PlaterPresetComboBox *combo)
{
    m_preset_combo = combo;

    if (m_preset_combo)
    {
        m_preset_combo->Reparent(this);

        int em = wxGetApp().em_unit();

        // Allow the combo to shrink smaller than its default minimum
        m_preset_combo->SetMinSize(wxSize(1, -1));

        if (m_preset_combo->edit_btn)
            m_preset_combo->edit_btn->Hide(); // Hide edit button - we use save button instead

        // Create horizontal sizer for combo + save button
        auto *combo_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Preset combo takes remaining space (proportion 1)
        combo_sizer->Add(m_preset_combo, 1, wxEXPAND | wxRIGHT, em / 4);

        // Save button has fixed size (proportion 0) - on the right
        m_btn_save = new ScalableButton(this, wxID_ANY, "save");
        m_btn_save->SetToolTip(_L("Save current settings to preset"));
        m_btn_save->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnSavePreset(); });
        combo_sizer->Add(m_btn_save, 0, wxALIGN_CENTER_VERTICAL);

        // Insert at the top with horizontal expansion
        m_main_sizer->Insert(0, combo_sizer, 0, wxEXPAND | wxALL, em / 2);
        Layout();
    }
}

void ProcessSection::OnSavePreset()
{
    // Trigger save preset dialog
    if (m_plater)
    {
        wxGetApp().get_tab(Preset::TYPE_PRINT)->save_preset();
    }
}

void ProcessSection::UpdateFromConfig()
{
    if (m_settings_panel)
    {
        m_settings_panel->RefreshFromConfig();
    }
}

void ProcessSection::RebuildContent()
{
    if (m_settings_panel)
        m_settings_panel->RebuildContent();
}

void ProcessSection::msw_rescale()
{
    if (m_settings_panel)
        m_settings_panel->msw_rescale();
}

void ProcessSection::sys_color_changed()
{
    if (m_settings_panel)
        m_settings_panel->sys_color_changed();
}

// ============================================================================
// Sidebar Implementation
// ============================================================================

Sidebar::Sidebar(Plater *parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
    , m_plater(parent)
    , m_scrolled_panel(nullptr)
    , m_main_sizer(nullptr)
    , m_printer_section(nullptr)
    , m_filament_section(nullptr)
    , m_process_section(nullptr)
    , m_objects_section(nullptr)
    , m_objects_content(nullptr)
    , m_printer_content(nullptr)
    , m_printer_settings_panel(nullptr)
    , m_filament_content(nullptr)
    , m_filament_settings_panel(nullptr)
    , m_process_content(nullptr)

    , m_combo_printer(nullptr)
    , m_combo_print(nullptr)
    , m_filaments_sizer(nullptr)
    , m_btn_save_printer(nullptr)
    , m_btn_edit_physical_printer(nullptr)
    , m_btn_save_filament(nullptr)
    , m_btn_save_print(nullptr)
    , m_object_list(nullptr)
    , m_object_manipulation(nullptr)
    , m_object_settings(nullptr)
    , m_object_layers(nullptr)
    , m_object_info(nullptr)
    , m_sliced_info(nullptr)
    , m_buttons_panel(nullptr)
    , m_btn_reslice(nullptr)
    , m_btn_export_gcode(nullptr)
    , m_btn_send_gcode(nullptr)
    , m_btn_connect_gcode(nullptr)
    , m_btn_export_gcode_removable(nullptr)
{
    int em = wxGetApp().em_unit();
    // Fixed sidebar width: 45 em units (matches Preview legend sidebar)
    int width = 45 * em;
    SetMinSize(wxSize(width, -1));
    SetSize(wxSize(width, -1));

    BuildUI();
    LoadSectionStates();
}

Sidebar::~Sidebar()
{
    SaveSectionStates();
}

void Sidebar::BuildUI()
{
    int em = wxGetApp().em_unit();

    // Set proper background color using unified accessor
    SetBackgroundColour(SidebarColors::Background());

    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // Scrolled panel for sections - match old Sidebar exactly
    m_scrolled_panel = new wxScrolledWindow(this);
    m_scrolled_panel->SetScrollRate(0, 5);
    m_scrolled_panel->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);

    SetFont(wxGetApp().normal_font());
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDarkUI(m_scrolled_panel);
    // UNIFIED THEMING: Always apply DarkMode_Explorer for scrollbar theming
    NppDarkMode::SetDarkExplorerTheme(m_scrolled_panel->GetHWND());
    m_scrolled_panel->SetDoubleBuffered(true);
#endif

    auto *scroll_sizer = new wxBoxSizer(wxVERTICAL);

    // Create collapsible sections
    CreatePrinterSection();
    CreateFilamentSection();
    CreateProcessSection();
    CreateObjectsSection();

    // Order: Print Settings, Filament Settings, Printer Settings, Object Settings
    scroll_sizer->Add(m_process_section, 0, wxEXPAND | wxALL, em / 4);
    scroll_sizer->Add(m_filament_section, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);
    scroll_sizer->Add(m_printer_section, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);
    // Object Settings gets proportion 1 to fill remaining space (ObjectList expands)
    scroll_sizer->Add(m_objects_section, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);

    m_scrolled_panel->SetSizer(scroll_sizer);
    m_scrolled_panel->FitInside();

    m_main_sizer->Add(m_scrolled_panel, 1, wxEXPAND);

    SetSizer(m_main_sizer);

    // Bind preset combo selection handler - this triggers actual preset changes
    this->Bind(wxEVT_COMBOBOX, &Sidebar::on_select_preset, this);

    // Force layout recalculation after window is fully shown to avoid dead space
    CallAfter(
        [this]()
        {
            if (m_objects_section)
                m_objects_section->Layout();
            m_scrolled_panel->FitInside();
            m_scrolled_panel->Layout();
            Layout();
            SendSizeEvent();
        });

    // Bind dead space click handlers to commit field changes
    BindDeadSpaceHandlers(m_scrolled_panel);
}

void Sidebar::BindDeadSpaceHandlers(wxWindow *root)
{
    if (!root)
        return;

    // Recursively bind to all container panels (but not input controls)
    std::function<void(wxWindow *)> bind_handler = [this, &bind_handler](wxWindow *win)
    {
        // Only bind to container types, skip all input controls
        bool isContainer = win->IsKindOf(CLASSINFO(wxPanel)) || win->IsKindOf(CLASSINFO(wxScrolledWindow));

        if (isContainer)
        {
            win->Bind(wxEVT_LEFT_DOWN,
                      [this](wxMouseEvent &evt)
                      {
                          wxWindow *focused = wxWindow::FindFocus();

                          // If a text input has focus, move focus away to commit the value
                          if (focused &&
                              (focused->IsKindOf(CLASSINFO(wxTextCtrl)) || focused->IsKindOf(CLASSINFO(wxSpinCtrl)) ||
                               focused->IsKindOf(CLASSINFO(wxSpinCtrlDouble))))
                          {
                              // Try object list first (it's a proper focusable DataViewCtrl)
                              if (m_object_list)
                              {
                                  m_object_list->SetFocus();
                              }
                              else
                              {
                                  // Fallback: navigate forward to move focus
                                  focused->Navigate(wxNavigationKeyEvent::IsForward);
                              }
                          }
                          evt.Skip(); // Always let the event continue
                      });
        }

        // Recurse to all children
        for (wxWindow *child : win->GetChildren())
        {
            if (child)
            {
                bind_handler(child);
            }
        }
    };

    bind_handler(root);
}

void Sidebar::CreatePrinterSection()
{
    m_printer_section = new CollapsibleSection(m_scrolled_panel, _L("Printer Settings"), false);
    m_printer_section->SetHeaderIcon(*get_bmp_bundle("printer"));
    // Top-level sections use a slightly darker header to distinguish from sub-tabs
    {
        bool is_dark = wxGetApp().dark_mode();
        StateColor sc;
        sc.append(is_dark ? UIColors::SectionHeaderHoverDark() : UIColors::SectionHeaderHoverLight(),
                  StateColor::Hovered);
        sc.append(is_dark ? UIColors::SectionHeaderBackgroundDark() : UIColors::SectionHeaderBackgroundLight(),
                  StateColor::Normal);
        m_printer_section->SetHeaderBackgroundColor(sc);
    }

    int em = wxGetApp().em_unit();

    // Pinned content - always visible dropdowns (printer preset + filament combos)
    m_printer_content = new wxPanel(m_printer_section, wxID_ANY);
    // Set proper colors for dark mode
    m_printer_content->SetBackgroundColour(SidebarColors::Background());
    m_printer_content->SetForegroundColour(SidebarColors::Foreground());
    auto *pinned_sizer = new wxBoxSizer(wxVERTICAL);

    // Printer preset combo with save button
    m_combo_printer = new PlaterPresetComboBox(m_printer_content, Preset::TYPE_PRINTER);
    m_combo_printer->SetMinSize(wxSize(1, -1)); // Allow combo to shrink
    m_combo_printer->SetForegroundColour(SidebarColors::Foreground());

    if (m_combo_printer->edit_btn)
        m_combo_printer->edit_btn->Hide(); // Hide edit button - we use save button instead

    auto *combo_sizer = new wxBoxSizer(wxHORIZONTAL);

    combo_sizer->Add(m_combo_printer, 1, wxEXPAND | wxRIGHT, em / 4);

    m_btn_save_printer = new ScalableButton(m_printer_content, wxID_ANY, "save");
    m_btn_save_printer->SetToolTip(_L("Save current settings to preset"));
    m_btn_save_printer->Bind(wxEVT_BUTTON,
                             [this](wxCommandEvent &) { wxGetApp().get_tab(Preset::TYPE_PRINTER)->save_preset(); });
    combo_sizer->Add(m_btn_save_printer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);

    m_btn_edit_physical_printer = new ScalableButton(m_printer_content, wxID_ANY, "cog");
    m_btn_edit_physical_printer->SetToolTip(_L("Edit physical printer"));
    m_btn_edit_physical_printer->Bind(wxEVT_BUTTON,
                                      [this](wxCommandEvent &)
                                      {
                                          if (!wxGetApp().preset_bundle->physical_printers.has_selection())
                                          {
                                              // No physical printer selected - open dialog to add one
                                              PhysicalPrinterDialog dlg(wxGetApp().mainframe, wxEmptyString);
                                              dlg.CentreOnParent();
                                              if (dlg.ShowModal() == wxID_OK)
                                              {
                                                  m_combo_printer->update();
                                                  wxGetApp().show_printer_webview_tab();
                                              }
                                          }
                                          else
                                          {
                                              // Edit the selected physical printer
                                              PhysicalPrinterDialog dlg(wxGetApp().mainframe,
                                                                        m_combo_printer->GetString(
                                                                            m_combo_printer->GetSelection()));
                                              dlg.CentreOnParent();
                                              if (dlg.ShowModal() == wxID_OK)
                                              {
                                                  m_combo_printer->update();
                                                  wxGetApp().show_printer_webview_tab();
                                              }
                                          }
                                      });
    combo_sizer->Add(m_btn_edit_physical_printer, 0, wxALIGN_CENTER_VERTICAL);

    pinned_sizer->Add(combo_sizer, 0, wxEXPAND | wxALL, em / 2);

    // Filament combos for each extruder (quick selection without needing to go to Filaments section)
    m_printer_filament_sizer = new wxBoxSizer(wxVERTICAL);
    pinned_sizer->Add(m_printer_filament_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 2);

    m_printer_content->SetSizer(pinned_sizer);
    m_printer_section->SetPinnedContent(m_printer_content);

    // Collapsible content - printer settings panel
    m_printer_settings_panel = new PrinterSettingsPanel(m_printer_section, m_plater);
    m_printer_section->SetContent(m_printer_settings_panel);

    m_printer_section->SetOnExpandChanged([this](bool expanded) { OnSectionExpandChanged("Printer", expanded); });

    // Initialize filament combos
    UpdatePrinterFilamentCombos();
}

void Sidebar::CreateFilamentSection()
{
    m_filament_section = new CollapsibleSection(m_scrolled_panel, _L("Filament Settings"), false);
    m_filament_section->SetHeaderIcon(*get_bmp_bundle("spool"));
    // Top-level sections use a slightly darker header to distinguish from sub-tabs
    {
        bool is_dark = wxGetApp().dark_mode();
        StateColor sc;
        sc.append(is_dark ? UIColors::SectionHeaderHoverDark() : UIColors::SectionHeaderHoverLight(),
                  StateColor::Hovered);
        sc.append(is_dark ? UIColors::SectionHeaderBackgroundDark() : UIColors::SectionHeaderBackgroundLight(),
                  StateColor::Normal);
        m_filament_section->SetHeaderBackgroundColor(sc);
    }

    m_filament_content = new wxPanel(m_filament_section, wxID_ANY);
    // Set proper colors for dark mode
    m_filament_content->SetBackgroundColour(SidebarColors::Background());
    m_filament_content->SetForegroundColour(SidebarColors::Foreground());
    m_filaments_sizer = new wxBoxSizer(wxVERTICAL);

    int em = wxGetApp().em_unit();

    // Initial filament combo with save button
    PlaterPresetComboBox *combo = nullptr;
    init_filament_combo(&combo, 0);

    if (combo->edit_btn)
        combo->edit_btn->Hide(); // Hide edit button - we use save button instead
    m_combos_filament.push_back(combo);

    auto *combo_sizer = new wxBoxSizer(wxHORIZONTAL);

    combo_sizer->Add(combo, 1, wxEXPAND | wxRIGHT, em / 4);

    m_btn_save_filament = new ScalableButton(m_filament_content, wxID_ANY, "save");
    m_btn_save_filament->SetToolTip(_L("Save current settings to preset"));
    m_btn_save_filament->Bind(wxEVT_BUTTON,
                              [this](wxCommandEvent &) { wxGetApp().get_tab(Preset::TYPE_FILAMENT)->save_preset(); });
    combo_sizer->Add(m_btn_save_filament, 0, wxALIGN_CENTER_VERTICAL);

    m_filaments_sizer->Add(combo_sizer, 0, wxEXPAND | wxALL, em / 2);

    // Filament settings panel with all filament settings
    m_filament_settings_panel = new FilamentSettingsPanel(m_filament_content, m_plater);
    m_filaments_sizer->Add(m_filament_settings_panel, 1, wxEXPAND);

    m_filament_content->SetSizer(m_filaments_sizer);
    m_filament_section->SetContent(m_filament_content);

    m_filament_section->SetOnExpandChanged([this](bool expanded) { OnSectionExpandChanged("Filament", expanded); });
}

void Sidebar::CreateProcessSection()
{
    m_process_section = new CollapsibleSection(m_scrolled_panel, _L("Print Settings"), false);
    m_process_section->SetHeaderIcon(*get_bmp_bundle("cog"));
    // Top-level sections use a slightly darker header to distinguish from sub-tabs
    {
        bool is_dark = wxGetApp().dark_mode();
        StateColor sc;
        sc.append(is_dark ? UIColors::SectionHeaderHoverDark() : UIColors::SectionHeaderHoverLight(),
                  StateColor::Hovered);
        sc.append(is_dark ? UIColors::SectionHeaderBackgroundDark() : UIColors::SectionHeaderBackgroundLight(),
                  StateColor::Normal);
        m_process_section->SetHeaderBackgroundColor(sc);
    }

    int em = wxGetApp().em_unit();

    // Pinned content - always visible print preset dropdown
    auto *pinned_panel = new wxPanel(m_process_section, wxID_ANY);
    pinned_panel->SetBackgroundColour(SidebarColors::Background());
    pinned_panel->SetForegroundColour(SidebarColors::Foreground());
    auto *pinned_sizer = new wxBoxSizer(wxVERTICAL);

    m_combo_print = new PlaterPresetComboBox(pinned_panel, Preset::TYPE_PRINT);
    m_combo_print->SetMinSize(wxSize(1, -1)); // Allow combo to shrink
    m_combo_print->SetForegroundColour(SidebarColors::Foreground());

    if (m_combo_print->edit_btn)
        m_combo_print->edit_btn->Hide(); // Hide edit button - we use save button instead

    auto *combo_sizer = new wxBoxSizer(wxHORIZONTAL);
    combo_sizer->Add(m_combo_print, 1, wxEXPAND | wxRIGHT, em / 4);

    m_btn_save_print = new ScalableButton(pinned_panel, wxID_ANY, "save");
    m_btn_save_print->SetToolTip(_L("Save current settings to preset"));
    m_btn_save_print->Bind(wxEVT_BUTTON,
                           [this](wxCommandEvent &) { wxGetApp().get_tab(Preset::TYPE_PRINT)->save_preset(); });
    combo_sizer->Add(m_btn_save_print, 0, wxALIGN_CENTER_VERTICAL);

    pinned_sizer->Add(combo_sizer, 0, wxEXPAND | wxALL, em / 2);
    pinned_panel->SetSizer(pinned_sizer);
    m_process_section->SetPinnedContent(pinned_panel);

    // Collapsible content - ProcessSection with print settings
    m_process_content = new ProcessSection(m_process_section, m_plater);
    m_process_section->SetContent(m_process_content);

    m_process_section->SetOnExpandChanged([this](bool expanded)
                                          { OnSectionExpandChanged("Print Settings", expanded); });
}

void Sidebar::CreateObjectsSection()
{
    m_objects_section = new CollapsibleSection(m_scrolled_panel, _L("Object Settings"), true);
    m_objects_section->SetHeaderIcon(*get_bmp_bundle("shape_gallery"));
    // Top-level sections use a slightly darker header to distinguish from sub-tabs
    {
        bool is_dark = wxGetApp().dark_mode();
        StateColor sc;
        sc.append(is_dark ? UIColors::SectionHeaderHoverDark() : UIColors::SectionHeaderHoverLight(),
                  StateColor::Hovered);
        sc.append(is_dark ? UIColors::SectionHeaderBackgroundDark() : UIColors::SectionHeaderBackgroundLight(),
                  StateColor::Normal);
        m_objects_section->SetHeaderBackgroundColor(sc);
    }

    // Get the content container directly from CollapsibleSection to avoid deep nesting
    // wxDataViewCtrl (ObjectList) doesn't work well with reparenting/deep panel nesting on Windows
    wxWindow *content_container = m_objects_section->GetContentContainer();

#ifdef _WIN32
    // Apply dark explorer theme for proper DataViewCtrl header theming
    // NOTE: Do NOT call UpdateDarkUI on CollapsibleSection or its content_container
    // as it overrides the themed background colors that CollapsibleSection sets
    if (wxGetApp().dark_mode())
    {
        NppDarkMode::SetDarkExplorerTheme(m_objects_section->GetHWND());
        NppDarkMode::SetDarkExplorerTheme(content_container->GetHWND());
    }
#endif

    int margin_5 = int(0.5 * wxGetApp().em_unit());

    // Create sizer for the content container directly (no intermediate panel)
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    // Object list - parent is the content container directly
    m_object_list = new ObjectList(content_container);
    sizer->Add(m_object_list->get_sizer(), 1, wxEXPAND);

#ifdef _WIN32
    // Ensure dark theme is applied after ObjectList is fully created
    if (wxGetApp().dark_mode())
    {
        wxGetApp().UpdateDVCDarkUI(m_object_list, true);
        NppDarkMode::SetDarkExplorerTheme(m_object_list->GetHWND());
    }
#endif

    // Object manipulation (transform controls)
    m_object_manipulation = new ObjectManipulation(content_container);
    m_object_manipulation->Hide();
    sizer->Add(m_object_manipulation->get_sizer(), 0, wxEXPAND | wxTOP, margin_5);

    // Object info (size, volume, facets, mesh status)
    m_object_info = new ObjectInfo(content_container);
    sizer->Add(m_object_info, 0, wxEXPAND | wxTOP, margin_5);

    // Object settings
    m_object_settings = new ObjectSettings(content_container);
    m_object_settings->Hide();
    sizer->Add(m_object_settings->get_sizer(), 0, wxEXPAND | wxTOP, margin_5);

    // Object layers
    m_object_layers = new ObjectLayers(content_container);
    m_object_layers->Hide();
    sizer->Add(m_object_layers->get_sizer(), 0, wxEXPAND | wxTOP, margin_5);

    content_container->SetSizer(sizer);

    // Don't call SetContent - we're adding directly to the container
    // m_objects_content is no longer used for Object Settings
    m_objects_content = nullptr;

    m_objects_section->SetOnExpandChanged([this](bool expanded)
                                          { OnSectionExpandChanged("Object Settings", expanded); });

    // Initialize extruder column visibility based on current printer preset
    CallAfter(
        [this]()
        {
            if (m_object_list && wxGetApp().preset_bundle)
            {
                const auto *nozzle_diameter = dynamic_cast<const ConfigOptionFloats *>(
                    wxGetApp().preset_bundle->printers.get_edited_preset().config.option("nozzle_diameter"));
                if (nozzle_diameter)
                {
                    m_object_list->update_objects_list_extruder_column(nozzle_diameter->values.size());
                }
            }

#ifdef _WIN32
            // Apply dark mode styling to all static text at startup
            if (wxGetApp().dark_mode() && m_scrolled_panel)
                wxGetApp().UpdateAllStaticTextDarkUI(m_scrolled_panel);
#endif
        });
}

void Sidebar::init_filament_combo(PlaterPresetComboBox **combo, int extr_idx)
{
    *combo = new PlaterPresetComboBox(m_filament_content, Preset::TYPE_FILAMENT);
    (*combo)->SetMinSize(wxSize(1, -1)); // Allow combo to shrink
    (*combo)->set_extruder_idx(extr_idx);
    (*combo)->SetForegroundColour(SidebarColors::Foreground());
}

void Sidebar::remove_unused_filament_combos(size_t current_count)
{
    while (m_combos_filament.size() > current_count)
    {
        auto *combo = m_combos_filament.back();
        m_filaments_sizer->Detach(combo);
        combo->Destroy();
        m_combos_filament.pop_back();
    }
}

void Sidebar::init_printer_filament_combo(PlaterPresetComboBox **combo, int extr_idx)
{
    *combo = new PlaterPresetComboBox(m_printer_content, Preset::TYPE_FILAMENT);
    (*combo)->SetMinSize(wxSize(1, -1)); // Allow combo to shrink
    (*combo)->set_extruder_idx(extr_idx);
    (*combo)->SetForegroundColour(SidebarColors::Foreground());

    // Hide the edit button - this is just for quick selection
    if ((*combo)->edit_btn)
        (*combo)->edit_btn->Hide();
}

void Sidebar::UpdatePrinterFilamentCombos()
{
    if (!m_printer_filament_sizer || !m_printer_content)
        return;

    // Get extruder count - use the minimum of nozzle_diameter count and extruders_filaments size
    // to avoid accessing uninitialized extruder filaments
    const auto *nozzle_diameter =
        wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter");
    size_t nozzle_count = nozzle_diameter ? nozzle_diameter->values.size() : 1;
    size_t filaments_count = wxGetApp().preset_bundle->extruders_filaments.size();
    size_t extruder_count = std::min(nozzle_count, filaments_count);

    int em = wxGetApp().em_unit();

    // Get original values from the SAVED preset (not the edited/in-memory version)
    // This way undo shows as modified only if user changed values from what's saved
    std::vector<double> original_nozzle_values;
    const Preset &saved_preset = wxGetApp().preset_bundle->printers.get_selected_preset();
    const auto *saved_nozzles = saved_preset.config.option<ConfigOptionFloats>("nozzle_diameter");
    if (saved_nozzles)
        original_nozzle_values = saved_nozzles->values;
    // Fallback to current values if saved preset doesn't have them
    if (original_nozzle_values.empty() && nozzle_diameter)
        original_nozzle_values = nozzle_diameter->values;

    wxColour bg_color = SidebarColors::Background();

    // Clear and rebuild if count changed
    if (m_printer_filament_combos.size() != extruder_count)
    {
        // Clear existing
        m_printer_filament_sizer->Clear(true); // true = delete windows
        m_printer_nozzle_lock_icons.clear();
        m_printer_nozzle_undo_icons.clear();
        m_printer_nozzle_original_values.clear();
        m_printer_nozzle_spins.clear();
        m_printer_filament_combos.clear();

        // Add header label
        auto *header_label = new wxStaticText(m_printer_content, wxID_ANY,
                                              _L("Nozzle diameter / Filament per extruder:"));
        // Apply theming to label - unified accessor handles both themes
        header_label->SetForegroundColour(SidebarColors::Foreground());
        m_printer_filament_sizer->Add(header_label, 0, wxBOTTOM, em / 4);

        // Add nozzle spin + filament combo rows
        for (size_t i = 0; i < extruder_count; ++i)
        {
            // Create horizontal sizer for this row
            auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

            // Get current and original nozzle diameter values
            double nozzle_value = 0.4;
            if (nozzle_diameter && i < nozzle_diameter->values.size())
                nozzle_value = nozzle_diameter->values[i];

            double original_value = nozzle_value;
            if (i < original_nozzle_values.size())
                original_value = original_nozzle_values[i];
            m_printer_nozzle_original_values.push_back(original_value);

            // Create lock icon
            auto *lock_icon = new wxStaticBitmap(m_printer_content, wxID_ANY, *get_bmp_bundle("lock_closed"));
            lock_icon->SetMinSize(GetScaledIconSizeWx());
            lock_icon->SetBackgroundColour(bg_color);
            lock_icon->SetToolTip(_L("Value is same as in the system preset"));
            m_printer_nozzle_lock_icons.push_back(lock_icon);
            row_sizer->Add(lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, GetIconMargin());

            // Create undo icon
            auto *undo_icon = new wxStaticBitmap(m_printer_content, wxID_ANY, *get_bmp_bundle("dot"));
            undo_icon->SetMinSize(GetScaledIconSizeWx());
            undo_icon->SetBackgroundColour(bg_color);
            m_printer_nozzle_undo_icons.push_back(undo_icon);
            row_sizer->Add(undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, GetIconMargin() * 2);

            // Undo click handler
            undo_icon->Bind(wxEVT_LEFT_DOWN,
                            [this, i](wxMouseEvent &)
                            {
                                if (i >= m_printer_nozzle_spins.size() || i >= m_printer_nozzle_original_values.size())
                                    return;

                                double original_value = m_printer_nozzle_original_values[i];
                                m_printer_nozzle_spins[i]->SetValue(original_value);

                                // Update printer config
                                auto &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
                                auto *nozzles = printer_config.option<ConfigOptionFloats>("nozzle_diameter");
                                if (nozzles && i < nozzles->values.size())
                                {
                                    nozzles->values[i] = original_value;

                                    // Sync to print preset
                                    auto &print_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
                                    auto *print_nozzles =
                                        print_config.option<ConfigOptionFloats>("print_nozzle_diameters", true);
                                    if (print_nozzles && i < print_nozzles->values.size())
                                        print_nozzles->values[i] = original_value;

                                    // Update tabs UI
                                    Tab *printer_tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
                                    if (printer_tab)
                                    {
                                        printer_tab->reload_config();
                                        printer_tab->update_dirty();
                                    }
                                    Tab *print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
                                    if (print_tab)
                                    {
                                        print_tab->reload_config();
                                        print_tab->update_dirty();
                                    }
                                }

                                update_nozzle_undo_ui(i);

                                // Also update the accordion panel's nozzle field and undo UI
                                if (m_printer_settings_panel)
                                    m_printer_settings_panel->RefreshFromConfig();
                            });

            // Create nozzle diameter spin control (width and height scaled with DPI)
            int em = wxGetApp().em_unit();
            int spin_width = int(5.5 * em);
            int spin_height = int(2.4 * em); // Proper height for spin control
            auto *spin = new SpinInputDouble(m_printer_content, wxString::Format("%.1f", nozzle_value), "",
                                             wxDefaultPosition, wxSize(spin_width, spin_height), 0, 0.1, 2.0,
                                             nozzle_value, 0.10);
            spin->SetDigits(1);
            m_printer_nozzle_spins.push_back(spin);

            // Event handler for nozzle diameter change
            spin->Bind(wxEVT_SPINCTRL,
                       [this, i](wxCommandEvent &)
                       {
                           if (i >= m_printer_nozzle_spins.size())
                               return;

                           double new_value = m_printer_nozzle_spins[i]->GetValue();

                           // Update printer config
                           auto &printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
                           auto *nozzles = printer_config.option<ConfigOptionFloats>("nozzle_diameter");
                           if (nozzles && i < nozzles->values.size())
                           {
                               nozzles->values[i] = new_value;

                               // Sync to print preset's print_nozzle_diameters
                               auto &print_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
                               auto *print_nozzles = print_config.option<ConfigOptionFloats>("print_nozzle_diameters",
                                                                                             true);
                               if (print_nozzles)
                               {
                                   while (print_nozzles->values.size() < nozzles->values.size())
                                       print_nozzles->values.push_back(nozzles->values[print_nozzles->values.size()]);
                                   if (i < print_nozzles->values.size())
                                       print_nozzles->values[i] = new_value;
                               }

                               // Update tabs UI
                               Tab *printer_tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
                               if (printer_tab)
                               {
                                   printer_tab->reload_config();
                                   printer_tab->update_dirty();
                               }
                               Tab *print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
                               if (print_tab)
                               {
                                   print_tab->reload_config();
                                   print_tab->update_dirty();
                               }
                           }

                           update_nozzle_undo_ui(i);

                           // Also update the accordion panel's nozzle field and undo UI
                           if (m_printer_settings_panel)
                               m_printer_settings_panel->RefreshFromConfig();
                       });

            row_sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);

            // Create filament combo
            PlaterPresetComboBox *combo = nullptr;
            init_printer_filament_combo(&combo, static_cast<int>(i));
            m_printer_filament_combos.push_back(combo);
            row_sizer->Add(combo, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

            m_printer_filament_sizer->Add(row_sizer, 0, wxEXPAND | wxBOTTOM, em / 4);

            // Initialize undo UI state
            update_nozzle_undo_ui(i);
        }
    }
    else
    {
        // Update original values from current parent preset
        m_printer_nozzle_original_values.clear();
        for (size_t i = 0; i < extruder_count; ++i)
        {
            double original_value = 0.4;
            if (i < original_nozzle_values.size())
                original_value = original_nozzle_values[i];
            m_printer_nozzle_original_values.push_back(original_value);
        }

        // Just update existing spin control values
        for (size_t i = 0; i < m_printer_nozzle_spins.size() && i < extruder_count; ++i)
        {
            if (m_printer_nozzle_spins[i] && nozzle_diameter && i < nozzle_diameter->values.size())
            {
                m_printer_nozzle_spins[i]->SetValue(nozzle_diameter->values[i]);
            }
        }

        // Update all undo UI states
        update_all_nozzle_undo_ui();
    }

    // Update all combo selections
    for (auto *combo : m_printer_filament_combos)
    {
        if (combo)
            combo->update();
    }

    m_printer_content->Layout();
}

void Sidebar::update_nozzle_undo_ui(size_t idx)
{
    if (idx >= m_printer_nozzle_spins.size() || idx >= m_printer_nozzle_lock_icons.size() ||
        idx >= m_printer_nozzle_undo_icons.size() || idx >= m_printer_nozzle_original_values.size())
        return;

    auto *spin = m_printer_nozzle_spins[idx];
    auto *lock_icon = m_printer_nozzle_lock_icons[idx];
    auto *undo_icon = m_printer_nozzle_undo_icons[idx];
    double original = m_printer_nozzle_original_values[idx];

    if (!spin || !lock_icon || !undo_icon)
        return;

    double current = spin->GetValue();
    bool is_modified = std::abs(current - original) > 0.001;

    // Update lock icon: lock_closed when unchanged, lock_open when modified
    lock_icon->SetBitmap(*get_bmp_bundle(is_modified ? "lock_open" : "lock_closed"));
    lock_icon->SetToolTip(is_modified ? _L("Value differs from system preset")
                                      : _L("Value is same as in system preset"));

    // Update undo icon: dot when unchanged, undo arrow when modified
    undo_icon->SetBitmap(*get_bmp_bundle(is_modified ? "undo" : "dot"));
    undo_icon->SetToolTip(is_modified ? _L("Click to revert to original value") : wxString(""));
}

void Sidebar::update_all_nozzle_undo_ui()
{
    for (size_t i = 0; i < m_printer_nozzle_spins.size(); ++i)
    {
        update_nozzle_undo_ui(i);
    }
}

void Sidebar::set_extruders_count(size_t count)
{
    // Update the printer section's filament combos when extruder count changes
    UpdatePrinterFilamentCombos();
    // Update the ObjectList extruder column visibility
    if (m_object_list)
    {
        m_object_list->update_objects_list_extruder_column(count);
    }
}

void Sidebar::update_objects_list_extruder_column(size_t count)
{
    if (m_object_list)
    {
        m_object_list->update_objects_list_extruder_column(count);
    }
}

void Sidebar::update_presets(Preset::Type preset_type)
{
    switch (preset_type)
    {
    case Preset::TYPE_PRINTER:
        if (m_combo_printer)
            m_combo_printer->update();
        UpdatePrinterFilamentCombos(); // Update extruder filament combos when printer changes
        // Update ObjectList extruder column visibility based on new printer's extruder count
        if (m_object_list && wxGetApp().preset_bundle)
        {
            const auto *nozzle_diameter = dynamic_cast<const ConfigOptionFloats *>(
                wxGetApp().preset_bundle->printers.get_edited_preset().config.option("nozzle_diameter"));
            if (nozzle_diameter)
            {
                m_object_list->update_objects_list_extruder_column(nozzle_diameter->values.size());
            }
        }
        if (m_printer_settings_panel)
            m_printer_settings_panel->RefreshFromConfig();
        break;
    case Preset::TYPE_PRINT:
        if (m_combo_print)
            m_combo_print->update();
        if (m_process_content)
            m_process_content->UpdateFromConfig();
        break;
    case Preset::TYPE_FILAMENT:
        for (auto *combo : m_combos_filament)
        {
            if (combo)
                combo->update();
        }
        // Also update the printer section's filament combos
        for (auto *combo : m_printer_filament_combos)
        {
            if (combo)
                combo->update();
        }
        if (m_filament_settings_panel)
            m_filament_settings_panel->RefreshFromConfig();
        break;
    default:
        break;
    }
}

void Sidebar::update_all_preset_comboboxes()
{
    if (m_combo_printer)
        m_combo_printer->update();
    if (m_combo_print)
        m_combo_print->update();
    for (auto *combo : m_combos_filament)
    {
        if (combo)
            combo->update();
    }
    for (auto *combo : m_printer_filament_combos)
    {
        if (combo)
            combo->update();
    }
}

void Sidebar::update_printer_presets_combobox()
{
    if (m_combo_printer)
        m_combo_printer->update();
}

void Sidebar::update_all_filament_comboboxes()
{
    for (auto *combo : m_combos_filament)
    {
        if (combo)
            combo->update();
    }
    for (auto *combo : m_printer_filament_combos)
    {
        if (combo)
            combo->update();
    }
}

void Sidebar::collapse(bool collapse)
{
    is_collapsed = collapse;
    Show(!collapse);

    if (GetParent())
    {
        GetParent()->Layout();
    }
}

void Sidebar::show_info_sizer(bool show)
{
    if (!m_object_info)
        return;

    if (!show)
    {
        m_object_info->Show(false);
        return;
    }

    Selection &selection = wxGetApp().plater()->canvas3D()->get_selection();
    ModelObjectPtrs objects = m_plater->model().objects;
    const int obj_idx = selection.get_object_idx();
    const int inst_idx = selection.get_instance_idx();

    if (objects.empty() || obj_idx < 0 || int(objects.size()) <= obj_idx || inst_idx < 0 ||
        int(objects[obj_idx]->instances.size()) <= inst_idx || objects[obj_idx]->volumes.empty() ||
        (selection.is_single_full_object() && objects[obj_idx]->instances.size() > 1) ||
        !(selection.is_single_full_instance() || selection.is_single_volume()))
    {
        m_object_info->Show(false);
        return;
    }

    const ModelObject *model_object = objects[obj_idx];

    bool imperial_units = wxGetApp().app_config->get_bool("use_inches");
    double koef = imperial_units ? ObjectManipulation::mm_to_in : 1.0f;

    ModelVolume *vol = nullptr;
    Transform3d t;
    if (selection.is_single_volume())
    {
        std::vector<int> obj_idxs, vol_idxs;
        wxGetApp().obj_list()->get_selection_indexes(obj_idxs, vol_idxs);
        if (vol_idxs.size() != 1)
            return;
        vol = model_object->volumes[vol_idxs[0]];
        t = model_object->instances[inst_idx]->get_matrix() * vol->get_matrix();
    }

    Vec3d size = vol ? vol->mesh().transformed_bounding_box(t).size()
                     : model_object->instance_bounding_box(inst_idx).size();
    m_object_info->info_size->SetLabel(
        wxString::Format("%.2f x %.2f x %.2f", size(0) * koef, size(1) * koef, size(2) * koef));

    const TriangleMeshStats &stats = vol ? vol->mesh().stats() : ModelProcessing::get_object_mesh_stats(model_object);

    double volume_val = stats.volume;
    if (vol)
        volume_val *= std::fabs(t.matrix().block(0, 0, 3, 3).determinant());

    m_object_info->info_volume->SetLabel(wxString::Format("%.2f", volume_val * pow(koef, 3)));
    m_object_info->info_facets->SetLabel(
        format_wxstr(_L_PLURAL("%1% (%2$d shell)", "%1% (%2$d shells)", stats.number_of_parts),
                     static_cast<int>(model_object->facets_count()), stats.number_of_parts));

    wxString info_manifold_label;
    auto mesh_errors = obj_list()->get_mesh_errors_info(&info_manifold_label);
    wxString tooltip = mesh_errors.tooltip;
    m_object_info->update_warning_icon(mesh_errors.warning_icon_name);
    m_object_info->info_manifold->SetLabel(info_manifold_label);
    m_object_info->info_manifold->SetToolTip(tooltip);
    m_object_info->manifold_warning_icon->SetToolTip(tooltip);

    m_object_info->show_sizer(true);
    if (vol || model_object->volumes.size() == 1)
        m_object_info->info_icon->Hide();

    if (m_plater->printer_technology() == ptSLA)
    {
        for (auto item : m_object_info->sla_hidden_items)
            item->Show(false);
    }
}

void Sidebar::show_sliced_info_sizer(bool show)
{
    // TODO: Implement sliced info display
}

void Sidebar::show_btns_sizer(bool show)
{
    if (m_buttons_panel)
    {
        m_buttons_panel->Show(show);
        Layout();
    }
}

void Sidebar::set_object_settings_mode(bool settings_visible)
{
    if (!m_object_list || !m_object_info || !m_object_settings)
        return;

    // Get the content container's sizer to change proportions
    wxWindow *content_container = m_object_list->GetParent();
    wxSizer *sizer = content_container ? content_container->GetSizer() : nullptr;

    if (settings_visible)
    {
        // Hide ObjectInfo to save space
        m_object_info->Show(false);

        // Minimize ObjectList to just show selected item + settings row (~3 rows worth)
        int row_height = wxGetApp().em_unit() * 2;                  // Approximate row height
        int compact_height = row_height * 3 + wxGetApp().em_unit(); // 3 rows + header
        m_object_list->SetMaxSize(wxSize(-1, compact_height));
        m_object_list->SetMinSize(wxSize(-1, compact_height));

        // Change sizer proportions: ObjectList gets 0, ObjectSettings gets 1
        if (sizer)
        {
            wxSizerItem *list_item = sizer->GetItem(m_object_list->get_sizer());
            wxSizerItem *settings_item = sizer->GetItem(m_object_settings->get_sizer());
            if (list_item)
                list_item->SetProportion(0);
            if (settings_item)
                settings_item->SetProportion(1);
        }
    }
    else
    {
        // Restore ObjectInfo visibility (show_info_sizer will be called separately)
        // Remove height constraints from ObjectList
        m_object_list->SetMaxSize(wxSize(-1, -1));
        m_object_list->SetMinSize(wxSize(-1, -1));

        // Restore sizer proportions: ObjectList gets 1, ObjectSettings gets 0
        if (sizer)
        {
            wxSizerItem *list_item = sizer->GetItem(m_object_list->get_sizer());
            wxSizerItem *settings_item = sizer->GetItem(m_object_settings->get_sizer());
            if (list_item)
                list_item->SetProportion(1);
            if (settings_item)
                settings_item->SetProportion(0);
        }
    }

    // Trigger layout update
    if (content_container)
        content_container->Layout();
    if (m_objects_section)
        m_objects_section->Layout();
}

void Sidebar::show_bulk_btns_sizer(bool show)
{
    // TODO: Implement bulk buttons sizer
}

void Sidebar::update_sliced_info_sizer()
{
    // TODO: Update sliced info
}

ConfigOptionsGroup *Sidebar::og_freq_chng_params(bool is_fff)
{
    // The new sidebar doesn't use FreqChangedParams in the same way
    // Return nullptr for now - callers should handle this gracefully
    return nullptr;
}

wxButton *Sidebar::get_wiping_dialog_button()
{
    // TODO: Implement wiping dialog button if needed
    return nullptr;
}

void Sidebar::enable_buttons(bool enable)
{
    if (m_btn_reslice)
        m_btn_reslice->Enable(enable);
    if (m_btn_export_gcode)
        m_btn_export_gcode->Enable(enable);
}

bool Sidebar::show_reslice(bool show)
{
    if (m_btn_reslice && m_btn_reslice->IsShown() != show)
    {
        m_btn_reslice->Show(show);
        return true;
    }
    return false;
}

bool Sidebar::show_export(bool show)
{
    if (m_btn_export_gcode && m_btn_export_gcode->IsShown() != show)
    {
        m_btn_export_gcode->Show(show);
        return true;
    }
    return false;
}

bool Sidebar::show_send(bool show)
{
    if (m_btn_send_gcode && m_btn_send_gcode->IsShown() != show)
    {
        m_btn_send_gcode->Show(show);
        return true;
    }
    return false;
}

bool Sidebar::show_export_removable(bool show)
{
    if (m_btn_export_gcode_removable && m_btn_export_gcode_removable->IsShown() != show)
    {
        m_btn_export_gcode_removable->Show(show);
        return true;
    }
    return false;
}

bool Sidebar::show_connect(bool show)
{
    if (m_btn_connect_gcode && m_btn_connect_gcode->IsShown() != show)
    {
        m_btn_connect_gcode->Show(show);
        return true;
    }
    return false;
}

bool Sidebar::show_export_all(bool show)
{
    // TODO: Implement bulk export button
    return false;
}

bool Sidebar::show_connect_all(bool show)
{
    // TODO: Implement bulk connect button
    return false;
}

bool Sidebar::show_export_removable_all(bool show)
{
    // TODO: Implement bulk removable export button
    return false;
}

void Sidebar::enable_bulk_buttons(bool enable)
{
    // TODO: Implement bulk buttons enabling
}

void Sidebar::switch_to_autoslicing_mode()
{
    // TODO: Implement autoslicing mode
}

void Sidebar::switch_from_autoslicing_mode()
{
    // TODO: Implement autoslicing mode exit
}

void Sidebar::set_btn_label(ActionButtonType type, const wxString &label)
{
    switch (type)
    {
    case ActionButtonType::Reslice:
        if (m_btn_reslice)
            m_btn_reslice->SetLabel(label);
        break;
    case ActionButtonType::Export:
        if (m_btn_export_gcode)
            m_btn_export_gcode->SetLabel(label);
        break;
    default:
        break;
    }
}

void Sidebar::update_mode()
{
    // TODO: Update based on Simple/Advanced/Expert mode
}

void Sidebar::update_ui_from_settings()
{
    // TODO: Update UI from app settings
}

void Sidebar::on_select_preset(wxCommandEvent &evt)
{
    PlaterPresetComboBox *combo = dynamic_cast<PlaterPresetComboBox *>(evt.GetEventObject());
    if (!combo)
    {
        evt.Skip(); // Not a preset combo, let event propagate
        return;
    }

    Preset::Type preset_type = combo->get_type();

    // Use GetSelection() from event parameter for OSX compatibility
    // (handles case-insensitive name matching issues)
    int selection = evt.GetSelection();
    auto idx = combo->get_extruder_idx();

    std::string preset_name = wxGetApp().preset_bundle->get_preset_name_by_alias(
        preset_type, Preset::remove_suffix_modified(into_u8(combo->GetString(selection))), idx);

    std::string last_selected_ph_printer_name = combo->get_selected_ph_printer_name();

    bool select_preset = !combo->selection_is_changed_according_to_physical_printers();

    if (preset_type == Preset::TYPE_FILAMENT)
    {
        wxGetApp().preset_bundle->set_filament_preset(idx, preset_name);

        TabFilament *tab = dynamic_cast<TabFilament *>(wxGetApp().get_tab(Preset::TYPE_FILAMENT));
        if (tab && combo->get_extruder_idx() == tab->get_active_extruder() && !tab->select_preset(preset_name))
        {
            // revert previously selection
            const std::string &old_name = wxGetApp().preset_bundle->filaments.get_edited_preset().name;
            wxGetApp().preset_bundle->set_filament_preset(idx, old_name);
        }
        else
            // Synchronize config.ini with the current selections
            wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
        combo->update();
    }
    else if (select_preset)
    {
        wxWindowUpdateLocker noUpdates(m_printer_content);
        wxGetApp().get_tab(preset_type)->select_preset(preset_name, false, last_selected_ph_printer_name);
    }

    if (preset_type != Preset::TYPE_PRINTER || select_preset)
    {
        // update plater with new config
        m_plater->on_config_change(wxGetApp().preset_bundle->full_config());
    }

    if (preset_type == Preset::TYPE_PRINTER)
    {
        // Settings list can be changed after printer preset changing
        // Also update for SLA vs FFF technology changes
        if (m_object_list)
            m_object_list->update_object_list_by_printer_technology();
        m_plater->update();
    }

#ifdef __WXMSW__
    // From Win 2004, preset combobox loses focus after change
    // Set focus back to combobox so up/down arrows work
    combo->SetFocus();
#endif
}

void Sidebar::OnSectionExpandChanged(const wxString &section_name, bool expanded)
{
    m_section_states[section_name] = expanded;

    // Single-section-open behavior: when one section expands, collapse all others
    if (expanded)
    {
        // Collapse all sections except the one that was just expanded
        if (section_name != "Printer" && m_printer_section && m_printer_section->IsExpanded())
        {
            m_printer_section->SetOnExpandChanged(nullptr);
            m_printer_section->SetExpanded(false);
            m_section_states["Printer"] = false;
        }
        if (section_name != "Filament" && m_filament_section && m_filament_section->IsExpanded())
        {
            m_filament_section->SetOnExpandChanged(nullptr);
            m_filament_section->SetExpanded(false);
            m_section_states["Filament"] = false;
        }
        if (section_name != "Print Settings" && m_process_section && m_process_section->IsExpanded())
        {
            m_process_section->SetOnExpandChanged(nullptr);
            m_process_section->SetExpanded(false);
            m_section_states["Print Settings"] = false;
        }
        if (section_name != "Object Settings" && m_objects_section && m_objects_section->IsExpanded())
        {
            m_objects_section->SetOnExpandChanged(nullptr);
            m_objects_section->SetExpanded(false);
            m_section_states["Object Settings"] = false;
        }

        // Re-enable callbacks
        if (m_printer_section)
        {
            m_printer_section->SetOnExpandChanged([this](bool exp) { OnSectionExpandChanged("Printer", exp); });
        }
        if (m_filament_section)
        {
            m_filament_section->SetOnExpandChanged([this](bool exp) { OnSectionExpandChanged("Filament", exp); });
        }
        if (m_process_section)
        {
            m_process_section->SetOnExpandChanged([this](bool exp) { OnSectionExpandChanged("Print Settings", exp); });
        }
        if (m_objects_section)
        {
            m_objects_section->SetOnExpandChanged([this](bool exp) { OnSectionExpandChanged("Object Settings", exp); });
        }
    }
    else
    {
        // When a section is collapsed, check if all sections are now collapsed
        bool all_collapsed = true;
        if (m_printer_section && m_printer_section->IsExpanded())
            all_collapsed = false;
        if (m_filament_section && m_filament_section->IsExpanded())
            all_collapsed = false;
        if (m_process_section && m_process_section->IsExpanded())
            all_collapsed = false;
        if (m_objects_section && m_objects_section->IsExpanded())
            all_collapsed = false;

        // If all sections are collapsed, auto-expand Object Settings
        if (all_collapsed && m_objects_section)
        {
            m_objects_section->SetExpanded(true);
            m_section_states["Object Settings"] = true;
        }
    }

    // Update sizer proportions - expanded section gets proportion 1, others get 0
    if (m_scrolled_panel && m_scrolled_panel->GetSizer())
    {
        wxSizer *sizer = m_scrolled_panel->GetSizer();

        // Find each section in the sizer and update its proportion
        for (size_t i = 0; i < sizer->GetItemCount(); i++)
        {
            wxSizerItem *item = sizer->GetItem(i);
            if (!item || !item->GetWindow())
                continue;

            wxWindow *win = item->GetWindow();
            int proportion = 0;

            // Give proportion 1 to the expanded section
            if (win == m_printer_section && m_printer_section->IsExpanded())
                proportion = 1;
            else if (win == m_filament_section && m_filament_section->IsExpanded())
                proportion = 1;
            else if (win == m_process_section && m_process_section->IsExpanded())
                proportion = 1;
            else if (win == m_objects_section && m_objects_section->IsExpanded())
                proportion = 1;

            item->SetProportion(proportion);
        }
    }

    m_scrolled_panel->Layout();
    m_scrolled_panel->FitInside();
    Layout();
}

void Sidebar::SaveSectionStates()
{
    // TODO: Save to app config
    // wxGetApp().app_config->set("sidebar_printer_expanded", m_section_states["Printer"] ? "1" : "0");
    // etc.
}

void Sidebar::LoadSectionStates()
{
    // TODO: Load from app config
    // For now, default Print Settings to expanded, others collapsed (single-section-open behavior)
    m_section_states["Printer"] = false;
    m_section_states["Filament"] = false;
    m_section_states["Print Settings"] = false;
    m_section_states["Object Settings"] = true;
}

void Sidebar::rebuild_settings_panels()
{
    // Rebuild all settings panels to reflect visibility changes
    if (m_printer_settings_panel)
        m_printer_settings_panel->RebuildContent();

    if (m_filament_settings_panel)
        m_filament_settings_panel->RebuildContent();

    if (m_process_content)
        m_process_content->RebuildContent();

    Layout();
}

// preFlight: Tab -> Sidebar sync. When a value changes in the main Tab,
// refresh the corresponding sidebar panel so controls and undo buttons stay in sync.
void Sidebar::refresh_settings_panel(Preset::Type type)
{
    switch (type)
    {
    case Preset::TYPE_PRINT:
        if (m_process_content)
            m_process_content->UpdateFromConfig();
        break;
    case Preset::TYPE_PRINTER:
        if (m_printer_settings_panel)
            m_printer_settings_panel->RefreshFromConfig();
        break;
    case Preset::TYPE_FILAMENT:
        if (m_filament_settings_panel)
            m_filament_settings_panel->RefreshFromConfig();
        break;
    default:
        break;
    }
}

void Sidebar::msw_rescale()
{
    int em = wxGetApp().em_unit();
    // Fixed sidebar width: 45 em units (matches Preview legend sidebar)
    int width = 45 * em;
    SetMinSize(wxSize(width, -1));
    SetSize(wxSize(width, -1));

    if (m_printer_section)
        m_printer_section->msw_rescale();
    if (m_filament_section)
        m_filament_section->msw_rescale();
    if (m_process_section)
        m_process_section->msw_rescale();
    if (m_objects_section)
        m_objects_section->msw_rescale();

    if (m_process_content)
        m_process_content->msw_rescale();

    if (m_printer_settings_panel)
        m_printer_settings_panel->msw_rescale();

    if (m_filament_settings_panel)
        m_filament_settings_panel->msw_rescale();

    if (m_object_list)
        m_object_list->msw_rescale();
    if (m_object_manipulation)
        m_object_manipulation->msw_rescale();
    // ObjectSettings doesn't have msw_rescale, only sys_color_changed
    if (m_object_layers)
        m_object_layers->msw_rescale();

    // Update nozzle icon sizes for DPI scaling
    wxSize icon_size = GetScaledIconSizeWx();
    for (auto *icon : m_printer_nozzle_lock_icons)
    {
        if (icon)
            icon->SetMinSize(icon_size);
    }
    for (auto *icon : m_printer_nozzle_undo_icons)
    {
        if (icon)
            icon->SetMinSize(icon_size);
    }

    // Update nozzle spin control sizes for DPI scaling
    int spin_width = int(5.5 * em);
    int spin_height = int(2.4 * em);
    for (auto *spin : m_printer_nozzle_spins)
    {
        if (spin)
        {
            spin->SetMinSize(wxSize(spin_width, spin_height));
            spin->SetSize(wxSize(spin_width, spin_height));
            spin->Rescale();
        }
    }

    // ScalableButton doesn't have msw_rescale, only sys_color_changed

    Layout();
}

void Sidebar::sys_color_changed()
{
#ifdef _WIN32
    wxWindowUpdateLocker noUpdates(this);
#endif

    // Use unified color accessor - no dark_mode() check needed
    wxColour bg_color = SidebarColors::Background();
    SetBackgroundColour(bg_color);

    if (m_scrolled_panel)
    {
        m_scrolled_panel->SetBackgroundColour(bg_color);
#ifdef _WIN32
        // UNIFIED THEMING: Always apply DarkMode_Explorer for scrollbar theming
        NppDarkMode::SetDarkExplorerTheme(m_scrolled_panel->GetHWND());
#endif
    }

#ifdef _WIN32
    // UNIFIED THEMING: Always apply DarkMode_Explorer for scrollbar theming
    if (m_objects_section)
    {
        wxWindow *content_container = m_objects_section->GetContentContainer();
        if (content_container)
            NppDarkMode::SetDarkExplorerTheme(content_container->GetHWND());
    }

    // Update ALL static text in scrolled panel for dark mode - this is the key call!
    wxGetApp().UpdateAllStaticTextDarkUI(m_scrolled_panel);
#endif

    // Update pinned content panels - use unified color accessor
    wxColour fg_color = SidebarColors::Foreground();
    if (m_printer_content)
    {
        m_printer_content->SetBackgroundColour(bg_color);
        m_printer_content->SetForegroundColour(fg_color);
    }
    if (m_filament_content)
    {
        m_filament_content->SetBackgroundColour(bg_color);
        m_filament_content->SetForegroundColour(fg_color);
    }
    // Update Print Settings pinned panel (contains print preset combo)
    if (m_process_section)
    {
        wxWindow *print_pinned = m_process_section->GetPinnedContent();
        if (print_pinned)
        {
            print_pinned->SetBackgroundColour(bg_color);
            print_pinned->SetForegroundColour(fg_color);
        }
    }

    // Re-apply top-level section header colors after sys_color_changed resets them
    auto apply_section_header_color = [](CollapsibleSection *section)
    {
        if (!section)
            return;
        bool is_dark = wxGetApp().dark_mode();
        StateColor sc;
        sc.append(is_dark ? UIColors::SectionHeaderHoverDark() : UIColors::SectionHeaderHoverLight(),
                  StateColor::Hovered);
        sc.append(is_dark ? UIColors::SectionHeaderBackgroundDark() : UIColors::SectionHeaderBackgroundLight(),
                  StateColor::Normal);
        section->SetHeaderBackgroundColor(sc);
    };

    // Update section header icons for new theme
    if (m_printer_section)
    {
        m_printer_section->SetHeaderIcon(*get_bmp_bundle("printer"));
        m_printer_section->sys_color_changed();
        apply_section_header_color(m_printer_section);
    }
    if (m_filament_section)
    {
        m_filament_section->SetHeaderIcon(*get_bmp_bundle("spool"));
        m_filament_section->sys_color_changed();
        apply_section_header_color(m_filament_section);
    }
    if (m_process_section)
    {
        m_process_section->SetHeaderIcon(*get_bmp_bundle("cog"));
        m_process_section->sys_color_changed();
        apply_section_header_color(m_process_section);
    }
    if (m_objects_section)
    {
        m_objects_section->SetHeaderIcon(*get_bmp_bundle("shape_gallery"));
        m_objects_section->sys_color_changed();
        apply_section_header_color(m_objects_section);
    }

    if (m_process_content)
        m_process_content->sys_color_changed();

    if (m_printer_settings_panel)
        m_printer_settings_panel->sys_color_changed();

    if (m_filament_settings_panel)
        m_filament_settings_panel->sys_color_changed();

    // Update preset combo boxes
    if (m_combo_printer)
    {
        m_combo_printer->SetBackgroundColour(bg_color);
        m_combo_printer->SetForegroundColour(fg_color);
        m_combo_printer->sys_color_changed();
    }
    if (m_combo_print)
    {
        m_combo_print->SetBackgroundColour(bg_color);
        m_combo_print->SetForegroundColour(fg_color);
        m_combo_print->sys_color_changed();
    }
    for (auto *combo : m_combos_filament)
    {
        if (combo)
        {
            combo->SetBackgroundColour(bg_color);
            combo->SetForegroundColour(fg_color);
            combo->sys_color_changed();
        }
    }
    for (auto *combo : m_printer_filament_combos)
    {
        if (combo)
        {
            combo->SetBackgroundColour(bg_color);
            combo->SetForegroundColour(fg_color);
            combo->sys_color_changed();
        }
    }
    for (auto *spin : m_printer_nozzle_spins)
    {
        if (spin)
            spin->SysColorsChanged();
    }
    for (auto *icon : m_printer_nozzle_lock_icons)
    {
        if (icon)
        {
            icon->SetBackgroundColour(bg_color);
            icon->Refresh();
        }
    }
    for (auto *icon : m_printer_nozzle_undo_icons)
    {
        if (icon)
        {
            icon->SetBackgroundColour(bg_color);
            icon->Refresh();
        }
    }
    // Refresh undo UI to update icon bitmaps for new theme
    update_all_nozzle_undo_ui();

    // Update ScalableButton icons
    if (m_btn_save_printer)
        m_btn_save_printer->sys_color_changed();
    if (m_btn_edit_physical_printer)
        m_btn_edit_physical_printer->sys_color_changed();
    if (m_btn_save_filament)
        m_btn_save_filament->sys_color_changed();
    if (m_btn_save_print)
        m_btn_save_print->sys_color_changed();

    // Update dynamic labels in printer section
    if (m_printer_filament_sizer && m_printer_content)
    {
        // Use unified color accessor
        wxColour label_color = SidebarColors::Foreground();
        for (wxSizerItem *item : m_printer_filament_sizer->GetChildren())
        {
            if (item && item->GetWindow())
            {
                if (wxStaticText *label = dynamic_cast<wxStaticText *>(item->GetWindow()))
                    label->SetForegroundColour(label_color);
            }
        }
    }

    if (m_object_list)
        m_object_list->sys_color_changed();
    if (m_object_manipulation)
        m_object_manipulation->sys_color_changed();
    if (m_object_info)
        m_object_info->sys_color_changed();
    if (m_object_settings)
        m_object_settings->sys_color_changed();
    if (m_object_layers)
        m_object_layers->sys_color_changed();

    Refresh();
}

} // namespace GUI
} // namespace Slic3r
