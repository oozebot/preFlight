///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Sidebar.hpp"
#include <algorithm>
#include <iterator>
#include <wx/wupdlock.h>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include "luminary/presets/app_config/AppConfig.hpp"
#include "luminary/platform/files/FileIO.hpp"
#include "luminary/platform/process/Process.hpp"
#include "Widgets/CollapsibleSection.hpp"
#include "Widgets/SwitchButton.hpp"
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
#include "ThumbnailsDialog.hpp"
#include "MsgDialog.hpp"

#include <wx/dcbuffer.h>
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
#include <set>
#include <wx/statbmp.h>
#include <wx/weakref.h>
#include <wx/colordlg.h>
#include <wx/clrpicker.h>

#include "luminary/config/thumbnails/Thumbnails.hpp"
#include "luminary/presets/preset/Preset.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"
#include "luminary/layer/settings_spec/SettingsSpec.hpp"
#include <string_view>
#include "luminary/model/scene/Model.hpp"
#include "luminary/model/edit/ModelProcessing.hpp"
#include "DSKY/Utils/PrintHost.hpp"
#include "DSKY/GUI/Selection.hpp"
#include "DSKY/GUI/GLCanvas3D.hpp"

#include <boost/algorithm/string.hpp>

using DSKY::format_wxstr;

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

namespace DSKY
{
using namespace Luminary;

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

// When true, "Edit Visibility" mode is active: every setting/group/tab is shown (with pin checkboxes)
// regardless of its pinned state. The three visibility gates below short-circuit on this flag.
static bool s_sidebar_edit_mode = false;

// Recursively collect all child windows contained (directly or via nested sizers) in a sizer.
static void collect_sizer_windows(wxSizer *sizer, std::set<wxWindow *> &out)
{
    if (!sizer)
        return;
    for (size_t i = 0; i < sizer->GetItemCount(); ++i)
    {
        wxSizerItem *item = sizer->GetItem(i);
        if (!item)
            continue;
        if (item->IsWindow())
            out.insert(item->GetWindow());
        else if (item->IsSizer())
            collect_sizer_windows(item->GetSizer(), out);
    }
}

// ============================================================================
// Helper to create wxStaticBoxSizer with FlatStaticBox for proper flat borders.
// The box title is left blank and drawn by an overlay header that also hosts a
// section pin checkbox (mirrors the main-settings section checkbox in OptionsGroup).
// ============================================================================
wxStaticBoxSizer *TabbedSettingsPanel::CreateFlatStaticBoxSizer(wxWindow *parent, const wxString &label, int orient)
{
    const int em = wxGetApp().em_unit();
    m_building_group = into_u8(label);

    // Blank box title; the overlay header below draws the real label.
    auto *stb = new FlatStaticBox(parent, wxID_ANY, " ");
#ifdef _WIN32
    stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
#endif
#ifdef __WXGTK__
    stb->SetFont(wxGetApp().normal_font());
#else
    stb->SetFont(wxOSX ? wxGetApp().normal_font() : wxGetApp().bold_font());
#endif
    wxGetApp().UpdateDarkUI(stb);

    auto *sizer = new wxStaticBoxSizer(stb, orient);
#if defined(__WXGTK__) || defined(__WXOSX__)
    // On GTK, the GtkFrame label is removed.  On macOS, the NSBox title is
    // set to NSNoTitle.  Add top padding so content clears the custom border.
    sizer->AddSpacer(em);
#endif

    // --- Overlay header: [pin checkbox][label] centered on the top border line ---
#ifdef __WXOSX__
    wxColour header_bg = parent->GetBackgroundColour();
#else
    wxColour header_bg = stb->GetBackgroundColour();
#endif
    // The overlay is a child of the box's PARENT, not the box itself: the setting rows are children
    // of `parent` too, and giving the wxStaticBox a child of its own changes how wxStaticBoxSizer
    // positions those rows (shifting all content out of alignment with the borders).
    auto *header_panel = new wxPanel(parent, wxID_ANY);
    header_panel->SetBackgroundColour(header_bg);
    auto *header_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto *section_cb = new ::CheckBox(header_panel);
    section_cb->SetValue(true);
    section_cb->SetToolTip(_L("Show this section in the sidebar"));

    auto *label_text = new wxStaticText(header_panel, wxID_ANY, label);
    label_text->SetFont(wxOSX ? wxGetApp().normal_font() : wxGetApp().bold_font());
    label_text->SetBackgroundColour(header_bg);
    wxGetApp().UpdateDarkUI(label_text);

    const int cb_gap = (em * 4) / 10;
    const int label_gap = em / 3;
    header_sizer->Add(section_cb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, cb_gap);
    header_sizer->Add(label_text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, label_gap);
    header_panel->SetSizer(header_sizer);
    header_panel->Fit();

    // Explicit widths: full (checkbox + gap + label + gap) for edit mode, label-only for normal mode
    // (Fit() may undersize before realization). Sizing explicitly avoids a leading gap where the
    // hidden checkbox would otherwise reserve space and indent the label.
    wxSize cbBest = section_cb->GetBestSize();
    wxSize txtExt = label_text->GetTextExtent(label);
    int full_w = cbBest.GetWidth() + cb_gap + txtExt.GetWidth() + label_gap;
    int label_w = txtExt.GetWidth() + label_gap;
    int needed_h = cbBest.GetHeight() > txtExt.GetHeight() ? cbBest.GetHeight() : txtExt.GetHeight();
    int init_w = s_sidebar_edit_mode ? full_w : label_w;
    header_panel->SetMinSize(wxSize(init_w, needed_h));
    header_panel->SetSize(init_w, needed_h);

    // Center the header on the top border line (drawn ~labelHeight/2 from the box top)
    int border_y = txtExt.GetHeight() / 2;
    int y_pos = border_y - (needed_h / 2) + 1;
#ifdef __WXOSX__
    if (y_pos < 0)
        y_pos = 0; // children above the content view top get clipped on macOS
#endif

    // Position the overlay over the box's top-left (parent coordinates) whenever the box is laid out.
    // header_panel is a weakref: stb (the event source) and header_panel are siblings that normally
    // die together, but the guard removes any teardown-order window where a queued SIZE/MOVE could
    // deref a freed header_panel (matching the PAINT handler below). stb stays raw: it is the bind
    // target, so it is always alive while its own handler runs.
    wxWeakRef<wxWindow> weak_header = header_panel;
    auto reposition_header = [weak_header, stb, y_pos]()
    {
        if (!weak_header)
            return;
        wxPoint p = stb->GetPosition();
        weak_header->SetPosition(wxPoint(p.x + 8, p.y + y_pos)); // x+8 matches static-box label inset
        weak_header->Raise();
    };
    // Z-order is set here (and in UpdateSectionCheckboxes), NOT per-paint: a sibling repaint never
    // covers a window above it, so re-raising on every paint is both unnecessary and unsafe.
    reposition_header();
    stb->Bind(wxEVT_SIZE,
              [reposition_header](wxSizeEvent &evt)
              {
                  reposition_header();
                  evt.Skip();
              });
    stb->Bind(wxEVT_MOVE,
              [reposition_header](wxMoveEvent &evt)
              {
                  reposition_header();
                  evt.Skip();
              });

    // The box paints its border with the siblings above it clipped out (FlatStaticBox on Windows),
    // so the overlay header is never painted over and needs no refresh from the box's paint path.
    // A refresh there re-invalidates the transparent box under the header and the two repaint each
    // other without end, which keeps a paint message pending and starves the idle pass that renders
    // the 3D scene.

    // Section checkbox pins/unpins every row in this group
    section_cb->Bind(wxEVT_CHECKBOX,
                     [this, sizer, section_cb](wxCommandEvent &evt)
                     {
                         SetGroupPinned(sizer, section_cb->GetValue());
                         evt.Skip();
                     });

    // Hidden (label only) outside edit mode; UpdateSectionCheckboxes reveals and refreshes it
    header_sizer->Show(section_cb, s_sidebar_edit_mode);
    header_panel->Layout();

    m_section_checkboxes.push_back(
        {section_cb, sizer, header_panel, stb, label_text, y_pos, full_w, label_w, needed_h});
    return sizer;
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

// Map a sidebar row key to the key used in the [sidebar_visibility] AppConfig section.
// The sidebar uses "nozzle_diameter" where the Tab stores visibility under "extruders_count".
static std::string sidebar_visibility_key(const std::string &key)
{
    return (key == "nozzle_diameter") ? std::string("extruders_count") : key;
}

// Raw pinned state for a single key (ignores edit mode): the exact same expression the Tab uses,
// so the sidebar checkbox and the main-settings checkbox reflect one shared value. Defaults to
// pinned (visible) when unset, and falls back to the indexed "key#0" variant the Tab stores for
// ConfigOptionFloats (machine limits, etc.).
static bool is_key_pinned(const std::string &key)
{
    const std::string effective_key = sidebar_visibility_key(key);
    std::string vis = get_app_config()->get("sidebar_visibility", effective_key);
    if (vis == "0")
        return false;
    if (vis == "1")
        return true;
    if (get_app_config()->get("sidebar_visibility", effective_key + "#0") == "0")
        return false;
    return true; // unset = pinned by default
}

// Write the pinned state for a key to the shared [sidebar_visibility] storage, saving immediately
// so it persists across sessions/crashes.
static void set_key_pinned(const std::string &key, bool pinned)
{
    get_app_config()->set("sidebar_visibility", sidebar_visibility_key(key), pinned ? "1" : "0");
    get_app_config()->save();
}

// Whether a single sidebar row should be shown. In edit mode every row is shown; otherwise it
// follows the shared pinned state (is_key_pinned), which the row checkbox reads and writes.
static bool is_sidebar_key_visible(const std::string &key)
{
    return s_sidebar_edit_mode ? true : is_key_pinned(key);
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
        bool is_themed = dynamic_cast<DSKY::ThemedTextCtrl *>(text) != nullptr;

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
// TabbedSettingsPanel Implementation - Base class for fixed-header settings panels
// ============================================================================

TabbedSettingsPanel::TabbedSettingsPanel(wxWindow *parent, Plater *plater)
    : wxPanel(parent, wxID_ANY), m_plater(plater), m_active_tab_index(0)
{
    // BuildUI() is not called here because it calls the virtual GetTabDefinitions().
    // Derived classes call BuildUI() in their own constructors.
}

void TabbedSettingsPanel::BuildUI()
{
    // Set background color using unified accessor
    SetBackgroundColour(SidebarColors::Background());

    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // Create a single ScrollablePanel that holds all sections in one scrollable list
    m_scroll_area = new ScrollablePanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_scroll_area->sys_color_changed();

    auto *content_panel = m_scroll_area->GetContentPanel();
    auto *content_sizer = new wxBoxSizer(wxVERTICAL);

    // Get tab definitions from subclass
    auto definitions = GetTabDefinitions();
    m_tabs.clear();
    m_tabs.reserve(definitions.size());

    // Create a collapsible section header for each category
    for (size_t i = 0; i < definitions.size(); ++i)
    {
        const auto &def = definitions[i];

        // Open unless the user closed it in an earlier session. Edit Visibility opens every
        // category and drops the chevron: you pin what you can see.
        auto *section = new CollapsibleSection(content_panel, def.title,
                                               s_sidebar_edit_mode || IsCategoryStoredExpanded(def));

        // Accent the section title so the category headers stand out from the rows below
        section->SetTitleAccent(true);

        // Set icon if available
        if (!def.icon_name.IsEmpty())
            section->SetHeaderIcon(*get_bmp_bundle(def.icon_name.ToStdString()));

        // No ancestor re-layout here or on a click: the expand callback lays out the scroll area
        section->SetCollapsible(!s_sidebar_edit_mode, false);
        section->SetRelayoutOnToggle(false);
        section->SetOnExpandChanged([this, i](bool expanded) { OnCategoryExpandChanged(i, expanded); });

        // Create plain content container inside section (no per-tab scroll)
        auto *container = new wxPanel(section, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
        container->SetBackgroundColour(SidebarColors::Background());
        auto *container_sizer = new wxBoxSizer(wxVERTICAL);
        container->SetSizer(container_sizer);
        section->SetContent(container);

        // Store tab state
        TabState state;
        state.definition = def;
        state.section = section;
        state.content_container = container;
        state.content = nullptr;
        state.content_built = false;
        m_tabs.push_back(std::move(state));

        // All sections at proportion 0 (natural height); a single scroll handles overflow
        content_sizer->Add(section, 0, wxEXPAND);
    }

    content_panel->SetSizer(content_sizer);

    // Add the single scroll area to fill the entire panel
    m_main_sizer->Add(m_scroll_area, 1, wxEXPAND);
    SetSizer(m_main_sizer);

    // Build content for all tabs eagerly so m_setting_controls is fully populated
    // (required for two-way sync between main settings tabs and sidebar)
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
        EnsureContentBuilt(i);

    // Set initial visibility: hide empty groups and sections
    UpdateSidebarVisibility();
}

void TabbedSettingsPanel::SwitchToTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    if (!m_scroll_area || !m_tabs[index].section)
        return;
    if (!m_tabs[index].section->IsShown())
        return;

    // Scroll to bring the requested section into view
    m_scroll_area->ScrollToChild(m_tabs[index].section);
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
        return m_tabs[m_active_tab_index].content_container;
    return nullptr;
}

wxPanel *TabbedSettingsPanel::GetContentArea(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_tabs.size()))
        return m_tabs[index].content_container;
    return nullptr;
}

void TabbedSettingsPanel::NoteRowPlacement(const std::string &registry_key)
{
    const std::string page = (m_active_tab_index >= 0 && m_active_tab_index < static_cast<int>(m_tabs.size()))
                                 ? into_u8(m_tabs[m_active_tab_index].definition.name)
                                 : std::string();
    m_row_placements.push_back({registry_key, page, m_building_group, static_cast<int>(m_row_placements.size())});
}

wxPanel *TabbedSettingsPanel::BuildPageFromSpec(const std::string &spec_page, size_t extruder_idx)
{
    auto *content = new wxPanel(GetContentArea(), wxID_ANY);
    content->SetBackgroundColour(SidebarColors::Background());
    content->SetForegroundColour(SidebarColors::Foreground());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    const int em = wxGetApp().em_unit();

    const unsigned preset_bit = setting_preset_bit(GetPresetType());
    std::vector<const SettingRow *> rows;
    for (const SettingRow &row : setting_rows())
        if ((row.presets & preset_bit) != 0 && row.sidebar_page != nullptr && spec_page == row.sidebar_page &&
            SpecRowShown(row))
            rows.push_back(&row);
    std::stable_sort(rows.begin(), rows.end(),
                     [](const SettingRow *a, const SettingRow *b) { return a->sidebar_order < b->sidebar_order; });

    wxWindow *host = content;
    wxStaticBoxSizer *group = nullptr;
    std::string group_title;
    for (const SettingRow *row : rows)
    {
        const char *title = row->group;
        if (group == nullptr || group_title != title)
        {
            const SpecGroupHost where = SpecGroupHostFor(content, sizer, *row);
            host = where.parent;
            BeforeSpecGroup(host, where.sizer, *row, extruder_idx);
            group = CreateFlatStaticBoxSizer(host, _L(title));
            where.sizer->Add(group, 0, wxEXPAND | wxALL, em / 4);
            group_title = title;
            OnSpecGroupOpened(host, group, *row);
        }
        const ConfigOptionDef *def = print_config_def.get(row->key);
        const wxString label = row->sidebar_label != nullptr ? _L(row->sidebar_label)
                               : def != nullptr              ? _L(def->label)
                                                             : wxString();
        CreateSpecRow(host, group, *row, label, extruder_idx);
        AfterSpecRow(host, group, *row, extruder_idx);
    }

    content->SetSizer(sizer);
    ApplyDarkModeToPanel(content);
    return content;
}

void TabbedSettingsPanel::EnsureAllContentBuilt()
{
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
        EnsureContentBuilt(i);
}

// A value may hold tabs and newlines (custom G-code); the dump keeps one row per line.
static std::string registry_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value)
    {
        if (c == '\t')
            out += "\\t";
        else if (c == '\n')
            out += "\\n";
        else if (c != '\r')
            out += c;
    }
    return out;
}

void TabbedSettingsPanel::DumpRegistry(const std::string &path) const
{
    boost::nowide::ofstream out(path);
    out << "page\tgroup\torder\tkey\twidget\tshown\tenabled\tvalue\treason\n";
    const DynamicPrintConfig &config = GetEditedConfig();
    for (const RowPlacement &row : m_row_placements)
    {
        RegistryRowInfo info;
        const bool found = LookupRegistryRow(row.key, info);
        const std::string base_key = row.key.substr(0, row.key.find('#'));
        const std::string value = config.has(base_key) ? config.opt_serialize(base_key) : std::string();
        out << row.page << '\t' << row.group << '\t' << row.order << '\t' << row.key << '\t'
            << (found ? info.widget_class : std::string("missing")) << '\t' << ((found && info.shown) ? 1 : 0) << '\t'
            << ((found && info.enabled) ? 1 : 0) << '\t' << registry_escape(value) << '\t'
            << registry_escape(found ? info.reason : std::string()) << '\n';
    }
}

void TabbedSettingsPanel::ApplyToggleRules()
{
    BeforeToggleRules();
    const std::vector<ToggleState> states = apply_toggle_rules(setting_preset_bit(GetPresetType()), GetEditedConfig(),
                                                               ToggleExtrudersCount());
    for (const ToggleState &state : states)
    {
        // A per-extruder row is registered as "key#<extruder>"
        const std::string registry_key = state.extruder >= 0 ? state.key + "#" + std::to_string(state.extruder)
                                                             : state.key;
        ApplyToggleState(registry_key, state.enabled, state.enabled ? wxString() : _(state.reason));
    }
}

void TabbedSettingsPanel::EnsureContentBuilt(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;

    if (m_tabs[index].content_built)
        return;

    // Freeze the content container to prevent layout thrashing during control creation
    if (m_tabs[index].content_container)
        m_tabs[index].content_container->Freeze();

    // Temporarily set active tab index so GetContentArea() (no-arg) returns the
    // correct content container for this tab. BuildXxxContent() methods use GetContentArea()
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

        // Add content to this tab's container (sizer already exists from BuildUI)
        auto *container_sizer = m_tabs[index].content_container->GetSizer();
        if (container_sizer)
        {
            container_sizer->Add(content, 0, wxEXPAND);
            m_tabs[index].content_container->Layout();
        }
    }
    m_tabs[index].content_built = true;

    if (m_tabs[index].content_container)
        m_tabs[index].content_container->Thaw();

    // Apply toggle logic to set initial enable/disable state of dependent options
    // This must be called after content is built so all controls exist
    ApplyToggleRules();

    // Bind dead space click handlers on new content to commit field changes
    // Use CallAfter to defer until after Plater construction is complete
    // (during construction, m_plater->sidebar() would crash because Plater::p is not yet assigned)
    // Re-fetch content from tab data when CallAfter fires rather than capturing a raw pointer,
    // because the content window can be destroyed/rebuilt before the deferred call executes.
    if (content && m_plater)
    {
        int tab_index = index;
        CallAfter(
            [this, tab_index]()
            {
                if (m_plater && tab_index >= 0 && tab_index < static_cast<int>(m_tabs.size()) &&
                    m_tabs[tab_index].content_built && m_tabs[tab_index].content)
                {
                    m_plater->sidebar().BindDeadSpaceHandlers(m_tabs[tab_index].content);
                }
            });
    }
}

void TabbedSettingsPanel::UpdateContentLayout()
{
    if (m_scroll_area)
    {
        m_scroll_area->Layout();
        m_scroll_area->UpdateScrollbar();
    }
    Layout();
}

void TabbedSettingsPanel::UpdateVisibilityCheckboxes()
{
    for (auto &[key, cb] : m_visibility_checkboxes)
    {
        if (!cb)
            continue;
        // Hide through the containing sizer so the row reclaims the space outside edit mode
        if (wxSizer *cs = cb->GetContainingSizer())
            cs->Show(cb, s_sidebar_edit_mode);
        else
            cb->Show(s_sidebar_edit_mode);
        if (s_sidebar_edit_mode) // refresh bitmap to the shared pinned state while editing
            cb->SetBitmap(*get_bmp_bundle(is_key_pinned(key) ? "check_on" : "check_off", 16));
    }
}

void TabbedSettingsPanel::SetGroupPinned(wxSizer *group_sizer, bool pinned)
{
    // Pin/unpin every row whose checkbox lives inside this group, and refresh its bitmap.
    std::set<wxWindow *> group_windows;
    collect_sizer_windows(group_sizer, group_windows);
    for (auto &[key, cb] : m_visibility_checkboxes)
    {
        if (!cb || !group_windows.count(cb))
            continue;
        set_key_pinned(key, pinned);
        cb->SetBitmap(*get_bmp_bundle(pinned ? "check_on" : "check_off", 16));
    }
}

void TabbedSettingsPanel::UpdateSectionCheckboxes()
{
    for (auto &sc : m_section_checkboxes)
    {
        if (!sc.checkbox)
            continue;

        // Show/hide the checkbox (label stays); size the overlay explicitly so the label has no
        // leading gap in normal mode, then keep it on the box border and on top.
        if (wxSizer *cs = sc.checkbox->GetContainingSizer())
            cs->Show(sc.checkbox, s_sidebar_edit_mode);
        else
            sc.checkbox->Show(s_sidebar_edit_mode);
        if (sc.header_panel && sc.box)
        {
            // The header overlay is a sibling of the box, not inside the group sizer, so it must
            // track the box's visibility explicitly. Otherwise an emptied group (all rows unpinned)
            // hides its box but leaves the label floating over the rows that move up to fill the gap.
            bool box_shown = sc.box->IsShown();
            sc.header_panel->Show(box_shown);
            if (box_shown)
            {
                int w = s_sidebar_edit_mode ? sc.full_w : sc.label_w;
                sc.header_panel->SetMinSize(wxSize(w, sc.height));
                sc.header_panel->SetSize(w, sc.height);
                sc.header_panel->Layout();
                wxPoint p = sc.box->GetPosition();
                sc.header_panel->SetPosition(wxPoint(p.x + 8, p.y + sc.y_pos));
                sc.header_panel->Raise();
            }
        }

        // Reflect the group state: checked if any row in the group is pinned
        if (s_sidebar_edit_mode)
        {
            std::set<wxWindow *> group_windows;
            collect_sizer_windows(sc.group_sizer, group_windows);
            bool any_pinned = false;
            for (auto &[key, cb] : m_visibility_checkboxes)
            {
                if (cb && group_windows.count(cb) && is_key_pinned(key))
                {
                    any_pinned = true;
                    break;
                }
            }
            sc.checkbox->SetValue(any_pinned);
        }
    }
}

void TabbedSettingsPanel::UpdateSidebarVisibility()
{
    Freeze();

    // Step 0: Fold state. Edit Visibility opens every category; the normal view restores the
    // stored states. Runs before the walk so the content containers are shown or hidden first.
    ApplyCategoryStates();

    // Step 1: Let subclass show/hide individual rows based on sidebar_visibility config
    UpdateRowVisibility();

    // Step 1b: Hide all auxiliary rows before the sizer walk, so they don't keep groups visible
    for (auto &[aux_sizer, parent_sizer] : m_auxiliary_rows)
    {
        if (parent_sizer && aux_sizer)
            parent_sizer->Show(aux_sizer, false);
    }

    // Build a set of auxiliary sizers so the walk can skip them
    // (auxiliary rows are managed separately in step 3b, not by the group walk)
    std::set<wxSizer *> aux_sizer_set;
    for (auto &[aux_sizer, parent_sizer] : m_auxiliary_rows)
        if (aux_sizer)
            aux_sizer_set.insert(aux_sizer);

    // Step 2: Walk sizer hierarchy to show/hide groups and sections
    for (size_t tab_idx = 0; tab_idx < m_tabs.size(); ++tab_idx)
    {
        auto &tab = m_tabs[tab_idx];
        if (!tab.content || !tab.content_built)
            continue;

        wxSizer *content_sizer = tab.content->GetSizer();
        if (!content_sizer)
            continue;

        bool any_group_visible = false;

        // Iterate direct children of the content sizer (these are groups or panels)
        for (size_t gi = 0; gi < content_sizer->GetItemCount(); ++gi)
        {
            wxSizerItem *group_item = content_sizer->GetItem(gi);
            if (!group_item)
                continue;

            if (group_item->IsSizer())
            {
                wxSizer *group_sizer = group_item->GetSizer();

                // Skip auxiliary rows - they're managed in step 3b
                if (aux_sizer_set.count(group_sizer))
                    continue;

                // Sub-sizer = a group (wxStaticBoxSizer from CreateFlatStaticBoxSizer)
                bool any_row_visible = false;

                for (size_t ri = 0; ri < group_sizer->GetItemCount(); ++ri)
                {
                    wxSizerItem *row_item = group_sizer->GetItem(ri);
                    if (row_item && row_item->IsShown() &&
                        !row_item->IsSpacer()) // Exclude spacers from visibility check (credit: topisani)
                    {
                        any_row_visible = true;
                        break;
                    }
                }

                group_item->Show(any_row_visible);
                if (any_row_visible)
                    any_group_visible = true;
            }
            else if (group_item->IsWindow())
            {
                // Window item (e.g., m_marlin_limits_panel, buttons): check its sizer children
                wxWindow *win = group_item->GetWindow();
                if (win && win->IsShown())
                {
                    wxSizer *win_sizer = win->GetSizer();
                    if (win_sizer)
                    {
                        // Walk groups inside the sub-panel
                        bool any_sub_visible = false;
                        for (size_t si = 0; si < win_sizer->GetItemCount(); ++si)
                        {
                            wxSizerItem *sub_item = win_sizer->GetItem(si);
                            if (!sub_item)
                                continue;

                            if (sub_item->IsSizer())
                            {
                                wxSizer *sub_sizer = sub_item->GetSizer();

                                // Skip auxiliary rows - they're managed in step 3b
                                if (aux_sizer_set.count(sub_sizer))
                                    continue;

                                bool any_sub_row = false;
                                for (size_t ri = 0; ri < sub_sizer->GetItemCount(); ++ri)
                                {
                                    wxSizerItem *ri_item = sub_sizer->GetItem(ri);
                                    if (ri_item && ri_item->IsShown() &&
                                        !ri_item->IsSpacer()) // Exclude spacers (credit: topisani)
                                    {
                                        any_sub_row = true;
                                        break;
                                    }
                                }
                                sub_item->Show(any_sub_row);
                                if (any_sub_row)
                                    any_sub_visible = true;
                            }
                            else if (sub_item->IsShown())
                            {
                                any_sub_visible = true;
                            }
                        }
                        // Don't hide sub-panels managed by other logic (e.g., machine limits flavor switching)
                        if (any_sub_visible)
                            any_group_visible = true;
                    }
                    else
                    {
                        // Window without sizer (e.g., a button) counts as visible
                        any_group_visible = true;
                    }
                }
            }
        }

        // Step 3: Show/hide the section based on whether it has any visible groups
        if (tab.section)
            tab.section->Show(any_group_visible);
    }

    // Step 3b: Show auxiliary rows where their parent group/sizer still has visible setting content
    for (auto &[aux_sizer, parent_sizer] : m_auxiliary_rows)
    {
        if (!parent_sizer || !aux_sizer)
            continue;

        // Check if any non-auxiliary sibling item is still visible after the sizer walk
        bool any_sibling_visible = false;
        for (size_t i = 0; i < parent_sizer->GetItemCount(); ++i)
        {
            wxSizerItem *item = parent_sizer->GetItem(i);
            if (!item)
                continue;

            // Skip auxiliary sizers (including this one) when checking for visible siblings
            if (item->IsSizer() && aux_sizer_set.count(item->GetSizer()))
                continue;

            if (item->IsShown())
            {
                any_sibling_visible = true;
                break;
            }
        }

        parent_sizer->Show(aux_sizer, any_sibling_visible);
    }

    // Step 4: Re-apply individual row visibility.
    // wxSizerItem::Show(true) on Item_Sizer calls ShowItems(true) which recursively shows ALL
    // children, undoing the individual row hiding from step 1. Re-applying restores correct state.
    UpdateRowVisibility();

    // Step 4a: Apply pin-checkbox visibility LAST. Steps 2-4 call wxSizerItem::Show(true), which
    // recursively re-shows every child of a shown row (including the checkbox), so this must run
    // after them to keep checkboxes hidden outside Edit Visibility mode.
    UpdateVisibilityCheckboxes();
    UpdateSectionCheckboxes();

    // Step 5: Update layout and scrollbar
    UpdateContentLayout();

    Thaw();
}

void TabbedSettingsPanel::UpdateSizerProportions()
{
    // Every section sits at its natural height; the single scroll area handles overflow
    if (m_scroll_area)
    {
        m_scroll_area->Layout();
        m_scroll_area->UpdateScrollbar();
    }
}

// ============================================================================
// Category fold state
// ============================================================================

std::string TabbedSettingsPanel::CategoryStateKey(const TabDefinition &def) const
{
    const char *panel = "print";
    switch (GetPresetType())
    {
    case Preset::TYPE_FILAMENT:
        panel = "filament";
        break;
    case Preset::TYPE_PRINTER:
        panel = "printer";
        break;
    default:
        break;
    }
    return std::string(panel) + "/" + def.name.ToStdString();
}

bool TabbedSettingsPanel::IsCategoryStoredExpanded(const TabDefinition &def) const
{
    // Unset = open: a fresh install shows every category expanded
    return get_app_config()->get("sidebar_expanded", CategoryStateKey(def)) != "0";
}

void TabbedSettingsPanel::StoreCategoryExpanded(const TabDefinition &def, bool expanded)
{
    get_app_config()->set("sidebar_expanded", CategoryStateKey(def), expanded ? "1" : "0");
}

void TabbedSettingsPanel::SetCategoryExpanded(size_t index, bool expanded)
{
    // Programmatic change: the click callback must not store the state (guarded by the flag rather
    // than by detaching the callback, which may be the one executing), and no ancestor re-layout
    // per section; the visibility pass lays out once at its end.
    if (index >= m_tabs.size())
        return;
    TabState &tab = m_tabs[index];
    if (!tab.section || tab.section->IsExpanded() == expanded)
        return;
    m_applying_category_states = true;
    tab.section->SetExpanded(expanded, false, false);
    m_applying_category_states = false;
}

void TabbedSettingsPanel::OnCategoryExpandChanged(size_t index, bool expanded)
{
    // A header click. The section has already shown or hidden its content; store the state so the
    // sidebar reopens this way, then re-run the scroll layout so the list below closes up.
    if (m_applying_category_states || index >= m_tabs.size())
        return;
    StoreCategoryExpanded(m_tabs[index].definition, expanded);
    get_app_config()->save();
    UpdateContentLayout();
}

void TabbedSettingsPanel::ApplyCategoryStates()
{
    // Edit Visibility opens every category and drops the chevron (you pin what you can see); the
    // normal view restores the stored states, so a category the user closed stays closed.
    for (size_t i = 0; i < m_tabs.size(); ++i)
    {
        CollapsibleSection *section = m_tabs[i].section;
        if (!section)
            continue;
        SetCategoryExpanded(i, s_sidebar_edit_mode || IsCategoryStoredExpanded(m_tabs[i].definition));
        if (section->IsCollapsible() == s_sidebar_edit_mode)
            section->SetCollapsible(!s_sidebar_edit_mode, false);
    }
}

void TabbedSettingsPanel::RebuildContent()
{
    // Release any mouse capture before destroying windows, or NotifyCaptureLost
    // crashes on a window that is destroyed while it still holds the capture.
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

    // Clear setting controls map and auxiliary rows BEFORE destroying windows
    // This prevents stale pointers from being accessed in ApplyToggleRules()
    ClearSettingControls();
    m_row_placements.clear();
    m_auxiliary_rows.clear();
    m_visibility_checkboxes.clear(); // raw pointers into about-to-be-destroyed rows
    m_section_checkboxes.clear();    // raw pointers into about-to-be-destroyed group headers

    // Destroy the scroll area, which destroys all child sections and content.
    // Hide it first to remove it from the DWM composition tree, preventing hundreds
    // of "invalid handle" errors as each child HWND is destroyed.
    if (m_scroll_area)
    {
#ifdef _WIN32
        // Direct Win32 hide is immediate and removes from DWM tracking
        if (m_scroll_area->GetHWND())
            ::ShowWindow((HWND) m_scroll_area->GetHWND(), SW_HIDE);
#else
        m_scroll_area->Hide();
#endif
        m_scroll_area->Destroy();
        m_scroll_area = nullptr;
    }

    // Clear tab state
    for (auto &tab : m_tabs)
    {
        tab.section = nullptr;
        tab.content_container = nullptr;
        tab.content = nullptr;
        tab.content_built = false;
    }
    m_tabs.clear();

    // Clear our sizer
    GetSizer()->Clear(false);

    // Rebuild everything with the panel frozen: the sections create hundreds of controls and
    // every one of them would otherwise reposition and repaint its parents.
    {
        wxWindowUpdateLocker frozen(this);
        BuildUI();
        Layout();
    }
}

void TabbedSettingsPanel::ScheduleRebuild(std::function<void()> after)
{
    if (after)
        m_after_rebuild.emplace_back(std::move(after));
    if (m_rebuild_pending)
        return;
    m_rebuild_pending = true;
    auto alive = m_alive;
    CallAfter(
        [this, alive]()
        {
            if (!*alive)
                return;
            m_rebuild_pending = false;
            RebuildContent();
            std::vector<std::function<void()>> after_rebuild;
            after_rebuild.swap(m_after_rebuild);
            for (auto &fn : after_rebuild)
                fn();
        });
}

void TabbedSettingsPanel::ApplyDarkModeToPanel(wxWindow *window)
{
    ApplyDarkModeToStaticBoxes(window);
}

void ApplySidebarTheme(wxWindow *root)
{
    ApplyDarkModeToStaticBoxes(root);
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

wxStaticBitmap *TabbedSettingsPanel::AddPinCheckbox(wxWindow *parent, wxSizer *left_sizer, const std::string &opt_key)
{
    wxColour bg_color = SidebarColors::Background();

    // Pin checkbox - leads the row, mirroring the main-settings checkbox (same check_on/check_off
    // bitmaps and the same shared [sidebar_visibility] key, so its state matches the Tab page exactly).
    // Shown only in Edit Visibility mode; clicking toggles whether the setting stays pinned.
    auto *checkbox = new wxStaticBitmap(parent, wxID_ANY,
                                        *get_bmp_bundle(is_key_pinned(opt_key) ? "check_on" : "check_off", 16));
    checkbox->SetMinSize(GetScaledIconSizeWx());
    checkbox->SetBackgroundColour(bg_color);
    checkbox->SetToolTip(_L("Show this setting in the sidebar"));
    {
        wxStaticBitmap *cb = checkbox;
        const std::string key = opt_key;
        cb->Bind(wxEVT_LEFT_DOWN,
                 [this, cb, key](wxMouseEvent &)
                 {
                     const bool now_pinned = !is_key_pinned(key);
                     set_key_pinned(key, now_pinned);
                     cb->SetBitmap(*get_bmp_bundle(now_pinned ? "check_on" : "check_off", 16));
                     cb->Refresh();
                     // Keep the owning section-header tri-state in sync with this row's pin state
                     UpdateSectionCheckboxes();
                 });
    }
    left_sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());
    // Hide through the sizer so no gap is reserved outside edit mode
    left_sizer->Show(checkbox, s_sidebar_edit_mode);
    m_visibility_checkboxes.emplace_back(opt_key, checkbox);
    return checkbox;
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
    ctx.tooltip = ctx.opt_def->tooltip.empty() ? wxString() : _(ctx.opt_def->tooltip);

    // Create row sizer
    ctx.row_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Left side sizer: icons + label (proportion 1 = 50% of row)
    ctx.left_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    // Pin checkbox - leads the row, mirroring the main-settings checkbox.
    ctx.visibility_checkbox = AddPinCheckbox(parent, ctx.left_sizer, opt_key);

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
    // Rescale the single scroll area
    if (m_scroll_area)
        m_scroll_area->msw_rescale();

    // Rescale each section header
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

    // Update single scroll area
    if (m_scroll_area)
        m_scroll_area->sys_color_changed();

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

        if (tab.content)
            ApplyDarkModeToPanel(tab.content);
    }

    // Let subclass update its content
    OnSysColorChanged();

    Refresh();
}

void TabbedSettingsPanel::ReapplyTitleAccents()
{
    // Re-assert accent header colors after an external dark-UI pass (e.g. the sidebar's build-time
    // UpdateDarkUI) reset every wxStaticText to the themed default. Lightweight: only re-sets colors.
    for (auto &tab : m_tabs)
        if (tab.section)
            tab.section->SetTitleAccent(true);
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

wxPanel *PrintSettingsPanel::BuildTabContent(int tab_index)
{
    if (tab_index < 0 || tab_index >= GetTabCount())
        return nullptr;
    return BuildPageFromSpec(into_u8(GetTabName(tab_index)));
}

void PrintSettingsPanel::CreateSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, const wxString &label,
                                       size_t /*extruder_idx*/)
{
    CreateSettingRow(parent, group, row.key, label, row.full_width);
}

// The "Set all widths" button follows the default extrusion width row.
void PrintSettingsPanel::AfterSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, size_t /*extruder_idx*/)
{
    if (std::string_view(row.key) != "extrusion_width")
        return;

    const int em = wxGetApp().em_unit();

    // Copies the default extrusion width into every width row below it; centered on its own line.
    auto *btn_row_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_row_sizer->AddStretchSpacer(1);
    auto *btn = new ScalableButton(parent, wxID_ANY, "copy", _L("Set all widths to default extrusion width"),
                                   wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
    btn->SetToolTip(_L("Set all extrusion widths below to match the Default extrusion width"));
    btn->Bind(wxEVT_BUTTON,
              [this](wxCommandEvent &)
              {
                  DynamicPrintConfig &config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
                  auto *default_width = config.option<ConfigOptionFloatOrPercent>("extrusion_width");
                  if (!default_width)
                      return;

                  static const std::vector<std::string> width_keys = {"first_layer_extrusion_width",
                                                                      "perimeter_extrusion_width",
                                                                      "external_perimeter_extrusion_width",
                                                                      "infill_extrusion_width",
                                                                      "solid_infill_extrusion_width",
                                                                      "bridge_extrusion_width",
                                                                      "top_infill_extrusion_width",
                                                                      "support_material_extrusion_width",
                                                                      "support_material_interface_extrusion_width"};

                  for (const auto &width_key : width_keys)
                      config.set_key_value(width_key, default_width->clone());

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
    group->Add(btn_row_sizer, 0, wxEXPAND | wxLEFT | wxBOTTOM, em / 4);
    m_auxiliary_rows.emplace_back(btn_row_sizer, group);
}

void PrintSettingsPanel::UpdateRowVisibility()
{
    for (auto &[key, ui] : m_setting_controls)
    {
        if (ui.row_sizer && ui.parent_sizer)
        {
            bool vis = is_sidebar_key_visible(key);
            ui.parent_sizer->Show(ui.row_sizer, vis);
        }
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

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : _(opt_def->tooltip);

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

    // Use wxEVT_KILL_FOCUS instead of wxEVT_TEXT to avoid triggering config writes,
    // tab syncs, and background slicing on every keystroke in multiline fields.
    text->Bind(wxEVT_KILL_FOCUS,
               [this, opt_key](wxFocusEvent &evt)
               {
                   OnSettingChanged(opt_key);
                   evt.Skip();
               });

    sizer->Add(text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);

    // Store UI elements
    SettingUIElements ui_elem;
    ui_elem.control = text;
    ui_elem.lock_icon = lock_icon;
    ui_elem.undo_icon = undo_icon;
    ui_elem.original_value = original_value;
    m_setting_controls[opt_key] = ui_elem;
    NoteRowPlacement(opt_key);

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
        ui_elem.row_sizer = row_sizer;
        ui_elem.parent_sizer = sizer;
        m_setting_controls[opt_key] = ui_elem;
        NoteRowPlacement(opt_key);

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

    // Hide row if sidebar visibility is off (row always created for show/hide toggling)
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        sizer->Hide(row_sizer);
}

void PrintSettingsPanel::OnSettingChanged(const std::string &opt_key)
{
    // Prevent cascading events during RefreshFromConfig or validation
    if (m_disable_update)
        return;

    // RAII guard: sets m_disable_update=true now, restores on scope exit (even if exception thrown)
    DisableUpdateGuard guard(m_disable_update);

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
        else if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            config.set_deserialize_strict(opt_key, into_u8(text_input->GetValue()));
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            config.set_deserialize_strict(opt_key, into_u8(text->GetValue()));
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

    // Serpentine coupling, matching the Print Settings tab: confirm on enable and revert if
    // declined, switch the perimeter generator to Athena on enable, and turn Serpentine off if the
    // generator moves away from Athena. The coupled controls do not reflect a programmatic edit
    // here until a refresh (RefreshFromConfig is inert while a change is in flight), so request one
    // once the current handler unwinds whenever the coupling changed a value.
    bool serp_coupling_changed = false;
    if (opt_key == "serpentine_enabled" && config.opt_bool("serpentine_enabled"))
    {
        MessageDialog dlg(wxGetApp().mainframe,
                          _L("Serpentine prints each region as a single continuous extrusion in place of "
                             "perimeters and infill, overriding most other print settings (perimeters, fill "
                             "density, fill pattern and related options). With a depth limit set, the "
                             "serpentine is confined to a band along the walls and the interior is filled "
                             "with normal infill.\n\n"
                             "Enable Serpentine?"),
                          _L("Serpentine"), wxICON_WARNING | wxYES | wxNO);
        if (dlg.ShowModal() != wxID_YES)
        {
            config.set_key_value("serpentine_enabled", new ConfigOptionBool(false));
            serp_coupling_changed = true;
        }
        else if (config.opt_enum<PerimeterGeneratorType>("perimeter_generator") != PerimeterGeneratorType::Athena)
        {
            config.set_key_value("perimeter_generator",
                                 new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Athena));
            serp_coupling_changed = true;
        }
    }
    else if (opt_key == "perimeter_generator" && config.opt_bool("serpentine_enabled") &&
             config.opt_enum<PerimeterGeneratorType>("perimeter_generator") != PerimeterGeneratorType::Athena)
    {
        config.set_key_value("serpentine_enabled", new ConfigOptionBool(false));
        serp_coupling_changed = true;
    }
    if (serp_coupling_changed)
        CallAfter([this]() { RefreshFromConfig(); });

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

    // Keep the legacy max_volumetric_speed alias in sync with max_volumetric_flow for
    // pre/post-processing scripts (mirrors the filament MVF/MVS sync and Tab.cpp).
    if (opt_key == "max_volumetric_flow")
    {
        auto *flow = config.option<ConfigOptionFloat>("max_volumetric_flow");
        auto *speed = config.option<ConfigOptionFloat>("max_volumetric_speed");
        if (flow && speed)
            speed->value = flow->value;
    }

    // Update the Print Settings tab to reflect the change
    // Note: Don't call update() as it runs validation again
    Tab *print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    if (print_tab)
    {
        print_tab->reload_config();
        print_tab->toggle_options();
        print_tab->update_description_lines();
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
    ApplyToggleRules();

    // Call SysColorsChanged on parent controls (TextInput, SpinInput, ComboBox)
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
    // Note: m_disable_update is reset by DisableUpdateGuard destructor
}

void PrintSettingsPanel::UpdateUndoUI(const std::string &opt_key)
{
    auto it = m_setting_controls.find(opt_key);
    if (it != m_setting_controls.end())
        UpdateUndoUICommon(opt_key, it->second.undo_icon, it->second.lock_icon, it->second.original_value);
}

void PrintSettingsPanel::RefreshFromConfig()
{
    // If we're already inside OnSettingChanged, don't refresh - this prevents the
    // circular callback: OnSettingChanged -> ConfigManipulation -> RefreshFromConfig()
    // from overwriting the user's in-progress edits with stale config values.
    if (m_disable_update)
        return;

    // RAII guard: sets m_disable_update=true now, restores on scope exit (even if exception thrown)
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
    ApplyToggleRules();
}

void PrintSettingsPanel::ResetOriginalValues()
{
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->prints.get_selected_preset().config;
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (config.has(opt_key))
            ui_elem.original_value = config.opt_serialize(opt_key);
    }
}

void PrintSettingsPanel::ApplyToggleState(const std::string &registry_key, bool enabled, const wxString &reason)
{
    ApplyToggleStateTo(m_setting_controls, registry_key, enabled, reason);
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

wxPanel *PrinterSettingsPanel::BuildTabContent(int tab_index)
{
    // Tabs are found by name: the extruder tabs and the single extruder MM tab come and go
    if (tab_index < 0 || tab_index >= GetTabCount())
        return nullptr;

    const wxString &tab_name = GetTabName(tab_index);
    if (tab_name.StartsWith("extruder_"))
    {
        // Every extruder tab renders the spec's extruder page with its own index
        long extruder_idx = 0;
        tab_name.Mid(9).ToLong(&extruder_idx);
        return BuildPageFromSpec("extruder_0", static_cast<size_t>(extruder_idx));
    }

    wxPanel *content = BuildPageFromSpec(into_u8(tab_name));
    if (tab_name == "limits")
        UpdateMachineLimitsVisibility();
    return content;
}

void PrinterSettingsPanel::CreateSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, const wxString &label,
                                         size_t extruder_idx)
{
    // The extruder pages repeat their rows per extruder. Every other page shows the plain row,
    // the machine limits' vector keys included: the sidebar edits their normal-mode value.
    if (std::string_view(row.sidebar_page).substr(0, 9) == "extruder_")
        CreateExtruderSettingRow(parent, group, row.key, label, extruder_idx);
    else
        CreateSettingRow(parent, group, row.key, label, row.full_width);
}

bool PrinterSettingsPanel::SpecRowShown(const SettingRow &row) const
{
    // The extruder offset only means something with several extruders
    return m_extruders_count > 1 || std::string_view(row.key) != "extruder_offset";
}

TabbedSettingsPanel::SpecGroupHost PrinterSettingsPanel::SpecGroupHostFor(wxWindow *content, wxSizer *sizer,
                                                                          const SettingRow &first_row)
{
    // The machine limits of each firmware family sit in a sub-panel the flavour shows or hides
    // (UpdateMachineLimitsVisibility); the sub-panel is created with its first group.
    const int em = wxGetApp().em_unit();
    const std::string_view key(first_row.key);
    const auto sub_panel = [&](wxPanel *&panel) -> SpecGroupHost
    {
        if (panel == nullptr)
        {
            panel = new wxPanel(content, wxID_ANY);
            panel->SetSizer(new wxBoxSizer(wxVERTICAL));
            sizer->Add(panel, 0, wxEXPAND);
        }
        return {panel, panel->GetSizer()};
    };

    if (key.substr(0, 12) == "machine_max_" || key.substr(0, 12) == "machine_min_")
    {
        const bool created = m_marlin_limits_panel == nullptr;
        const SpecGroupHost host = sub_panel(m_marlin_limits_panel);
        if (created)
        {
            // Stealth mode note, shown only while stealth mode is enabled (Marlin only)
            m_stealth_mode_note = new wxStaticText(
                host.parent, wxID_ANY, _L("Normal mode only. Edit Stealth in Printer Settings > Machine limits."));
            m_stealth_mode_note->SetFont(wxGetApp().small_font());
            m_stealth_mode_note->SetForegroundColour(UIColors::SecondaryText());
            m_stealth_mode_note->Hide();
            auto *note_sizer = new wxBoxSizer(wxHORIZONTAL);
            note_sizer->AddStretchSpacer(1);
            note_sizer->Add(m_stealth_mode_note, 0, wxALIGN_CENTER_VERTICAL);
            note_sizer->AddStretchSpacer(1);
            host.sizer->Add(note_sizer, 0, wxEXPAND | wxALL, em / 4);
            m_auxiliary_rows.emplace_back(note_sizer, host.sizer);
        }
        return host;
    }

    if (key.substr(0, 12) == "machine_rrf_")
    {
        const bool created = m_rrf_limits_panel == nullptr;
        const SpecGroupHost host = sub_panel(m_rrf_limits_panel);
        if (created)
        {
            // The "Retrieve from machine" line above the M-code fields
            auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
            auto *desc_text = new wxStaticText(host.parent, wxID_ANY, _L("Machine limit M-codes:"), wxDefaultPosition,
                                               wxDefaultSize, wxST_ELLIPSIZE_END);
            desc_text->SetMinSize(wxSize(1, -1)); // Allow text to shrink
            btn_sizer->Add(desc_text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);

            auto *retrieve_btn = new ScalableButton(host.parent, wxID_ANY, "refresh", _L("Retrieve from machine"),
                                                    wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
            retrieve_btn->SetToolTip(_L("Retrieve machine limits from connected printer"));
            retrieve_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnRetrieveFromMachine(); });
            btn_sizer->Add(retrieve_btn, 0, wxALIGN_CENTER_VERTICAL);
            host.sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, em / 4);
            m_auxiliary_rows.emplace_back(btn_sizer, host.sizer);
        }
        return host;
    }

    if (key.substr(0, 16) == "machine_klipper_")
        return sub_panel(m_klipper_limits_panel);

    return {content, sizer};
}

void PrinterSettingsPanel::BeforeSpecGroup(wxWindow *parent, wxSizer *sizer, const SettingRow &first_row,
                                           size_t extruder_idx)
{
    // The "apply to other extruders" button sits between the Preview and Cooling fan groups
    if (m_extruders_count > 1 && std::string_view(first_row.group) == "Cooling fan")
        AddApplyToOtherExtrudersButton(parent, sizer, extruder_idx);
}

void PrinterSettingsPanel::OnSpecGroupOpened(wxWindow *parent, wxSizer *group, const SettingRow &first_row)
{
    const std::string_view title(first_row.group);
    if (title == "Size and coordinates")
        AddBedShapeRow(parent, group);
    else if (title == "Capabilities")
        AddExtruderCountRow(parent, group);
    else if (title == "Klipper machine limits")
    {
        // The values mirror printer.cfg [printer]; only the time estimate reads them.
        const int em = wxGetApp().em_unit();
        auto *note = new wxStaticText(parent, wxID_ANY,
                                      _L("Enter values from your Klipper printer.cfg [printer] section.\n"
                                         "These are used for time estimation only."));
        note->SetFont(wxGetApp().small_font());
        note->SetForegroundColour(UIColors::SecondaryText());
        auto *note_sizer = new wxBoxSizer(wxHORIZONTAL);
        note_sizer->Add(note, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
        group->Add(note_sizer, 0, wxEXPAND | wxALL, em / 4);
        m_auxiliary_rows.emplace_back(note_sizer, group);
    }
}

void PrinterSettingsPanel::AfterSpecRow(wxWindow * /*parent*/, wxSizer * /*group*/, const SettingRow &row,
                                        size_t /*extruder_idx*/)
{
    if (std::string_view(row.key) != "machine_limits_usage")
        return;

    // Klipper, RRF and Rapid cannot emit the limits to G-code: that choice leaves the combo
    // (UpdateMachineLimitsUsageChoices keeps it out at run time; the initial build needs it too)
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

void PrinterSettingsPanel::AddApplyToOtherExtrudersButton(wxWindow *parent, wxSizer *sizer, size_t extruder_idx)
{
    const int em = wxGetApp().em_unit();
    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *btn = new ScalableButton(parent, wxID_ANY, "copy", _L("Apply below settings to other extruders"),
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
    m_auxiliary_rows.emplace_back(btn_sizer, sizer);
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
    ScheduleRebuild([this]() { m_disable_update = false; });
}

// The bed shape row: a label and the button that opens the bed shape editor, first in the
// Size and coordinates group.
void PrinterSettingsPanel::AddBedShapeRow(wxWindow *parent, wxSizer *group)
{
    const int em = wxGetApp().em_unit();
    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Left side: label (50% width)
    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *label = new wxStaticText(parent, wxID_ANY, _L("Bed shape:"), wxDefaultPosition, wxDefaultSize,
                                   wxST_ELLIPSIZE_END);
    label->SetMinSize(wxSize(1, -1));
    label->SetToolTip(_L("Shape and size of the print bed"));
    left_sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
    row_sizer->Add(left_sizer, 1, wxEXPAND);

    // Right side: button left-justified in value area (50% width)
    auto *right_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *btn = new ScalableButton(parent, wxID_ANY, "settings_white", _L("Set bed shape"), wxDefaultSize,
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

    group->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
    m_auxiliary_rows.emplace_back(row_sizer, group);
}

// The extruder count row, first in the Capabilities group: a spinner over the size of the
// nozzle_diameter vector, registered under that key so it locks and undoes like a row.
void PrinterSettingsPanel::AddExtruderCountRow(wxWindow *parent, wxSizer *group)
{
    const int em = wxGetApp().em_unit();
    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    // Lock icon - shows lock_closed when value matches system preset
    auto *lock_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("lock_closed"));
    lock_icon->SetMinSize(GetScaledIconSizeWx());
    lock_icon->SetBackgroundColour(bg_color);
    lock_icon->SetToolTip(_L("Value is same as in the system preset"));
    left_sizer->Add(lock_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    // Undo icon - shows dot when unchanged, undo arrow when modified
    auto *undo_icon = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("dot"));
    undo_icon->SetMinSize(GetScaledIconSizeWx());
    undo_icon->SetBackgroundColour(bg_color);
    left_sizer->Add(undo_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, GetIconMargin());

    auto *label_text = new wxStaticText(parent, wxID_ANY, _L("Extruders:"), wxDefaultPosition, wxDefaultSize,
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
    auto *spin = new SpinInput(parent, text_value, "", wxDefaultPosition, wxSize(GetScaledInputWidth(), -1), 0, 1, 256,
                               extruder_count);
    spin->SetToolTip(_L("Number of extruders of the printer."));

    // Store UI elements for undo tracking (use nozzle_diameter as the key)
    SettingUIElements ui_elem;
    ui_elem.control = spin;
    ui_elem.lock_icon = lock_icon;
    ui_elem.undo_icon = undo_icon;
    ui_elem.original_value = original_value;
    ui_elem.row_sizer = row_sizer;
    ui_elem.parent_sizer = group;
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
    group->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
}

bool PrinterSettingsPanel::ShouldShowSingleExtruderMM() const
{
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    bool semm_enabled = config.opt_bool("single_extruder_multi_material");
    return m_extruders_count > 1 && semm_enabled;
}

void PrinterSettingsPanel::UpdateMachineLimitsVisibility()
{
    if (!m_marlin_limits_panel || !m_rrf_limits_panel || !m_klipper_limits_panel)
        return;

    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    auto flavor = static_cast<GCodeFlavor>(config.option("gcode_flavor")->getInt());
    bool is_rrf = (flavor == gcfRepRapFirmware || flavor == gcfRapid);
    bool is_klipper = (flavor == gcfKlipper);

    // One limits panel per flavour family, as the Settings page shows them
    m_marlin_limits_panel->Show(!is_rrf && !is_klipper);
    m_rrf_limits_panel->Show(is_rrf);
    m_klipper_limits_panel->Show(is_klipper);

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

    // Thumbnails: the raw "WxH/FORMAT, ..." string is fragile to hand-edit, so the sidebar exposes an
    // "Edit thumbnails" button that opens the structured ThumbnailsDialog instead of a text field.
    if (opt_key == "thumbnails")
    {
        auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto *edit_btn = new ScalableButton(parent, wxID_ANY, "wrench", " " + _L("Edit thumbnails"), wxDefaultSize,
                                            wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        edit_btn->SetFont(wxGetApp().normal_font());
        edit_btn->SetSize(edit_btn->GetBestSize());
        // Snapshot the SAVED preset value (not the edited/in-memory one) so undo reverts to the
        // last-saved thumbnails even if the sidebar is rebuilt while the preset is dirty.
        const DynamicPrintConfig &saved_config = wxGetApp().preset_bundle->printers.get_selected_preset().config;
        if (saved_config.has("thumbnails"))
            original_value = saved_config.opt_serialize("thumbnails");

        // Refresh the tooltip from live config on hover so it never goes stale (preset switch, undo, etc.).
        edit_btn->Bind(wxEVT_ENTER_WINDOW,
                       [edit_btn](wxMouseEvent &e)
                       {
                           const DynamicPrintConfig &cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
                           edit_btn->SetToolTip(thumbnails_summary(cfg.has("thumbnails") ? cfg.opt_string("thumbnails")
                                                                                         : std::string()));
                           e.Skip();
                       });

        edit_btn->Bind(wxEVT_BUTTON,
                       [this](wxCommandEvent &)
                       {
                           DynamicPrintConfig &cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
                           const std::string cur = cfg.has("thumbnails") ? cfg.opt_string("thumbnails") : std::string();
                           ThumbnailsDialog dlg(wxGetApp().mainframe, cur);
                           if (dlg.ShowModal() != wxID_OK)
                               return;

                           // Mirror OnSettingChanged's order: guard against re-entrant RefreshFromConfig, then
                           // update the undo/lock icons BEFORE reloading the tab, so they settle correctly.
                           DisableUpdateGuard guard(m_disable_update);
                           cfg.set_key_value("thumbnails", new ConfigOptionString(dlg.get_value()));
                           UpdateUndoUI("thumbnails");
                           wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);
                           if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                           {
                               tab->reload_config();
                               tab->update_dirty();
                               tab->update_changed_ui();
                           }
                       });

        value_sizer->Add(edit_btn, 0, wxALIGN_CENTER_VERTICAL);
        value_sizer->AddStretchSpacer(1);
        row_sizer->Add(value_sizer, 1, wxEXPAND);

        SettingUIElements ui_elem;
        ui_elem.control = edit_btn;
        ui_elem.lock_icon = lock_icon;
        ui_elem.undo_icon = undo_icon;
        ui_elem.label_text = ctx.label_text;
        ui_elem.original_value = original_value;
        ui_elem.row_sizer = row_sizer;
        ui_elem.parent_sizer = sizer;
        m_setting_controls["thumbnails"] = ui_elem;
        NoteRowPlacement("thumbnails");
        UpdateUndoUI("thumbnails");

        // Undo icon reverts to the saved value (the button holds no value of its own).
        undo_icon->Bind(wxEVT_LEFT_DOWN,
                        [this](wxMouseEvent &)
                        {
                            auto it = m_setting_controls.find("thumbnails");
                            if (it == m_setting_controls.end())
                                return;
                            DisableUpdateGuard guard(m_disable_update);
                            // Revert to the live SAVED preset value (the snapshot can go stale across rebuilds).
                            const DynamicPrintConfig &saved_config =
                                wxGetApp().preset_bundle->printers.get_selected_preset().config;
                            const std::string saved_raw = saved_config.has("thumbnails")
                                                              ? saved_config.opt_string("thumbnails")
                                                              : std::string();
                            DynamicPrintConfig &cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
                            cfg.set_key_value("thumbnails", new ConfigOptionString(saved_raw));
                            it->second.original_value = saved_config.has("thumbnails")
                                                            ? saved_config.opt_serialize("thumbnails")
                                                            : std::string();
                            UpdateUndoUI("thumbnails");
                            wxGetApp().preset_bundle->printers.get_edited_preset().set_dirty(true);
                            if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
                            {
                                tab->reload_config();
                                tab->update_dirty();
                                tab->update_changed_ui();
                            }
                        });

        sizer->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
        return;
    }

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
        ui_elem.label_text = ctx.label_text;
        ui_elem.original_value = original_value;
        ui_elem.row_sizer = row_sizer;
        ui_elem.parent_sizer = sizer;
        m_setting_controls[opt_key] = ui_elem;
        NoteRowPlacement(opt_key);

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
    int em = wxGetApp().em_unit();

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : _(opt_def->tooltip);

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
    ui_elem.row_sizer = container_sizer;
    ui_elem.parent_sizer = sizer;
    m_setting_controls[opt_key] = ui_elem;
    NoteRowPlacement(opt_key);

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
    int em = wxGetApp().em_unit();

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : _(opt_def->tooltip);

    // Left side sizer: icons + label
    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    // Create composite key for tracking this specific extruder's setting
    std::string composite_key = opt_key + "#" + std::to_string(extruder_idx);

    // Pin checkbox leads the row, pinned per-extruder under the same composite key the row hides on
    AddPinCheckbox(parent, left_sizer, composite_key);

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
        ui_elem.row_sizer = row_sizer;
        ui_elem.parent_sizer = sizer;
        m_setting_controls[composite_key] = ui_elem;
        NoteRowPlacement(composite_key);

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

    // RAII guard: sets m_disable_update=true now, restores on scope exit (even if exception thrown)
    DisableUpdateGuard guard(m_disable_update);

    std::string composite_key = opt_key + "#" + std::to_string(extruder_idx);
    auto it = m_setting_controls.find(composite_key);
    if (it == m_setting_controls.end())
        return;

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

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
    ApplyToggleRules();

    // Update undo UI for this setting
    UpdateUndoUI(composite_key);

    // If nozzle_diameter changed, also update the top nozzle spinners
    if (opt_key == "nozzle_diameter")
    {
        wxGetApp().sidebar().refresh_printer_nozzles();
    }
    // Note: m_disable_update is reset by DisableUpdateGuard destructor
}

void PrinterSettingsPanel::OnSettingChanged(const std::string &opt_key)
{
    if (m_disable_update)
        return;

    // RAII guard: sets m_disable_update=true now, restores on scope exit (even if exception thrown)
    DisableUpdateGuard guard(m_disable_update);

    auto it = m_setting_controls.find(opt_key);
    if (it == m_setting_controls.end())
        return;

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

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
        else if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            config.set_deserialize_strict(opt_key, into_u8(text_input->GetValue()));
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            config.set_deserialize_strict(opt_key, into_u8(text->GetValue()));
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
        UpdateMachineLimitsVisibility();

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
        ScheduleRebuild();
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

    // The tab's update() is where it rebuilds its pages (the firmware family's machine limits,
    // stealth mode's second column, the single extruder MM page), enables and disables the
    // fields that depend on the changed one, and refreshes its description lines; the sync
    // above only re-reads values. It runs after the coupled-key handling so the page sees
    // the final config.
    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER))
        tab->update();

    // Trigger plater update
    if (GetPlater())
    {
        GetPlater()->on_config_change(config);
    }

    // Apply toggle logic to enable/disable dependent options
    ApplyToggleRules();
    // Note: m_disable_update is reset by DisableUpdateGuard destructor
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
            case coPoints:
                if (auto *opt = config.option<ConfigOptionPoints>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                    {
                        current_value = std::to_string(opt->values[extruder_idx].x()) + "x" +
                                        std::to_string(opt->values[extruder_idx].y());
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
    // If we're already inside OnSettingChanged, don't refresh - this prevents the
    // circular callback: OnSettingChanged -> tab->update_dirty() -> RefreshFromConfig()
    // from overwriting the user's in-progress edits with stale config values.
    if (m_disable_update)
        return;

    // RAII guard: sets m_disable_update=true now, restores on scope exit (even if exception thrown)
    DisableUpdateGuard guard(m_disable_update);

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
        ScheduleRebuild();
        return; // Let rebuild handle everything (guard destructor resets m_disable_update)
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
    ApplyToggleRules();

    // Call SysColorsChanged on parent controls (TextInput, SpinInput, ComboBox)
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
    // Note: m_disable_update is reset by DisableUpdateGuard destructor
}

void PrinterSettingsPanel::ResetOriginalValues()
{
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_selected_preset().config;
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        size_t hash_pos = opt_key.find('#');
        if (hash_pos != std::string::npos)
        {
            // Extruder-specific key - recompute original from the vector option
            std::string base_key = opt_key.substr(0, hash_pos);
            size_t extruder_idx = std::stoul(opt_key.substr(hash_pos + 1));

            const ConfigOptionDef *opt_def = print_config_def.get(base_key);
            if (!opt_def || !config.has(base_key))
                continue;

            switch (opt_def->type)
            {
            case coFloats:
            case coPercents:
                if (auto *opt = config.option<ConfigOptionFloats>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                        ui_elem.original_value = into_u8(wxString::Format("%g", opt->values[extruder_idx]));
                }
                break;
            case coBools:
                if (auto *opt = config.option<ConfigOptionBools>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                        ui_elem.original_value = opt->values[extruder_idx] ? "1" : "0";
                }
                break;
            case coInts:
                if (auto *opt = config.option<ConfigOptionInts>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                        ui_elem.original_value = std::to_string(opt->values[extruder_idx]);
                }
                break;
            case coStrings:
                if (auto *opt = config.option<ConfigOptionStrings>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                        ui_elem.original_value = opt->values[extruder_idx];
                }
                break;
            case coEnums:
                if (auto *opt = dynamic_cast<const ConfigOptionVectorBase *>(config.option(base_key)))
                {
                    auto serialized = opt->vserialize();
                    if (extruder_idx < serialized.size())
                        ui_elem.original_value = serialized[extruder_idx];
                }
                break;
            case coPoints:
                if (auto *opt = config.option<ConfigOptionPoints>(base_key))
                {
                    if (extruder_idx < opt->values.size())
                    {
                        ui_elem.original_value = std::to_string(opt->values[extruder_idx].x()) + "x" +
                                                 std::to_string(opt->values[extruder_idx].y());
                    }
                }
                break;
            default:
                break;
            }
        }
        else
        {
            if (config.has(opt_key))
                ui_elem.original_value = config.opt_serialize(opt_key);
        }
    }
    m_preserved_original_values.clear();
}

void PrinterSettingsPanel::ApplyToggleState(const std::string &registry_key, bool enabled, const wxString &reason)
{
    ApplyToggleStateTo(m_setting_controls, registry_key, enabled, reason);
}

void PrinterSettingsPanel::UpdateMachineLimitsUsageChoices()
{
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const GCodeFlavor flavor = config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value;

    // Klipper, RRF and Rapid keep their limits in their own configuration: no "Emit to G-code"
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

void PrinterSettingsPanel::UpdateRowVisibility()
{
    for (auto &[key, ui] : m_setting_controls)
    {
        if (ui.row_sizer && ui.parent_sizer)
        {
            bool vis = is_sidebar_key_visible(key);
            ui.parent_sizer->Show(ui.row_sizer, vis);
        }
    }
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

wxPanel *FilamentSettingsPanel::BuildTabContent(int tab_index)
{
    if (tab_index < 0 || tab_index >= GetTabCount())
        return nullptr;
    const std::string page = into_u8(GetTabName(tab_index));
    wxPanel *content = BuildPageFromSpec(page);
    if (page == "overrides")
        UpdateOverridesToggleState();
    return content;
}

void FilamentSettingsPanel::CreateSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row,
                                          const wxString &label, size_t /*extruder_idx*/)
{
    if (row.widget == SettingWidget::Nullable)
        CreateNullableSettingRow(parent, group, row.key, label);
    else
        CreateSettingRow(parent, group, row.key, label, row.full_width);
}

// The ramming row, a label and the button that opens the ramming editor, closes the
// single-extruder MMU group after its last row.
void FilamentSettingsPanel::AfterSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row,
                                         size_t /*extruder_idx*/)
{
    if (std::string_view(row.key) != "filament_purge_multiplier")
        return;

    const int em = wxGetApp().em_unit();
    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *label = new wxStaticText(parent, wxID_ANY, _L("Ramming:"), wxDefaultPosition, wxDefaultSize,
                                   wxST_ELLIPSIZE_END);
    label->SetMinSize(wxSize(1, -1));
    label->SetToolTip(_L("Ramming parameters for filament loading"));
    left_sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
    row_sizer->Add(left_sizer, 1, wxEXPAND);

    auto *right_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *btn = new ScalableButton(parent, wxID_ANY, "settings", _L("Ramming settings"), wxDefaultSize,
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

    group->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
    m_auxiliary_rows.emplace_back(row_sizer, group);
}

void FilamentSettingsPanel::CreateSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key,
                                             const wxString &label, bool full_width)
{
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
        ui_elem.row_sizer = row_sizer;
        ui_elem.parent_sizer = sizer;
        m_setting_controls[opt_key] = ui_elem;
        NoteRowPlacement(opt_key);

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
                                if (opt_key == "filament_colour")
                                {
                                    // Only restore the panel visual; the OnSettingChanged call
                                    // below reads it back and performs the single commit.
                                    if (auto *panel = dynamic_cast<wxPanel *>(it->second.control))
                                    {
                                        wxColour clr(from_u8(it->second.original_value));
                                        if (clr.IsOk())
                                        {
                                            panel->SetBackgroundColour(clr);
                                            panel->Refresh();
                                        }
                                    }
                                }
                                else if (auto *combo = dynamic_cast<::ComboBox *>(it->second.control))
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

    // Hide row if sidebar visibility is off (row always created for show/hide toggling)
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        sizer->Hide(row_sizer);
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

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : _(opt_def->tooltip);

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
    NoteRowPlacement(opt_key);

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
    int em = wxGetApp().em_unit();

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxString tooltip = opt_def->tooltip.empty() ? wxString() : _(opt_def->tooltip);

    // Left side sizer: icons + checkbox + label
    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Set background color using unified accessor
    wxColour bg_color = SidebarColors::Background();

    // Pin checkbox leads the row so overrides pin/hide like normal rows
    AddPinCheckbox(parent, left_sizer, opt_key);

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
        ui_elem.row_sizer = row_sizer;
        ui_elem.parent_sizer = sizer;
        m_setting_controls[opt_key] = ui_elem;
        NoteRowPlacement(opt_key);

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

    // Hide row if sidebar visibility is off (row always created for show/hide toggling)
    if (get_app_config()->get("sidebar_visibility", opt_key) == "0")
        sizer->Hide(row_sizer);
}

void FilamentSettingsPanel::OnNullableSettingChanged(const std::string &opt_key, bool is_checked)
{
    if (m_disable_update)
        return;

    // RAII guard: sets m_disable_update=true now, restores on scope exit (even if exception thrown)
    DisableUpdateGuard guard(m_disable_update);

    auto it = m_setting_controls.find(opt_key);
    if (it == m_setting_controls.end())
        return;

    DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_edited_preset().config;

    if (is_checked)
    {
        // Enable the field and restore last meaningful value
        if (it->second.control)
        {
            // TextInput wraps a wxTextCtrl but derives from wxWindow, not wxTextCtrl.
            // Check for TextInput first - its Enable() handles editable state + colors on Windows.
            if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
            {
                text_input->Enable(true);
            }
            else if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(it->second.control))
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
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            text_input->SetValue(from_u8(value_to_set));
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
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
            // TextInput wraps a wxTextCtrl but derives from wxWindow, not wxTextCtrl.
            // Check for TextInput first - its Enable() handles editable state + colors on Windows.
            if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
            {
                text_input->Enable(false);
            }
            else if (wxTextCtrl *text = dynamic_cast<wxTextCtrl *>(it->second.control))
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
        if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            std::string current = into_u8(text_input->GetValue());
            if (current != into_u8(_L("N/A")) && !current.empty())
                it->second.last_meaningful_value = current;
            text_input->SetValue(_L("N/A"));
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
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

    // Mark dirty and sync; update() enables and disables the tab's override fields
    wxGetApp().preset_bundle->filaments.get_edited_preset().set_dirty(true);

    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT))
    {
        tab->reload_config();
        tab->update_dirty();
        tab->update_changed_ui();
        tab->update();
    }

    if (GetPlater())
        GetPlater()->on_config_change(config);

    // Update toggle states for dependent options
    UpdateOverridesToggleState();

    // Update undo icon state
    UpdateUndoUI(opt_key);
    // Note: m_disable_update is reset by DisableUpdateGuard destructor
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

    // RAII guard: sets m_disable_update=true now, restores on scope exit (even if exception thrown)
    DisableUpdateGuard guard(m_disable_update);

    auto it = m_setting_controls.find(opt_key);
    if (it == m_setting_controls.end())
        return;

    const ConfigOptionDef *opt_def = print_config_def.get(opt_key);
    if (!opt_def)
        return;

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
        else if (auto *text_input = dynamic_cast<::TextInput *>(it->second.control))
        {
            // TextInput fallback for nullable coInts (e.g. idle_temperature)
            config.set_deserialize_strict(opt_key, into_u8(text_input->GetValue()));
        }
        else if (auto *text = dynamic_cast<wxTextCtrl *>(it->second.control))
        {
            config.set_deserialize_strict(opt_key, into_u8(text->GetValue()));
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
        // Special handling for filament_colour - the color commits straight to the preset instead
        // of entering the dirty state, so this path bypasses the generic set_dirty tail below.
        if (opt_key == "filament_colour")
        {
            if (auto *panel = dynamic_cast<wxPanel *>(it->second.control))
            {
                wxColour color = panel->GetBackgroundColour();
                wxString color_str = wxString::Format("#%02X%02X%02X", color.Red(), color.Green(), color.Blue());
                commit_filament_color_to_preset(wxGetApp().preset_bundle->filaments.get_edited_preset().name,
                                                into_u8(color_str));
                // The committed color is the new revert point; the undo arrow has nothing to offer.
                it->second.original_value = into_u8(color_str);
                UpdateUndoUI(opt_key);
            }
            return;
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

    // Mutual exclusion: manual fan speeds and auto cooling cannot both be enabled (auto cooling
    // adjusts fan speed per layer time, overriding manual values). Mirrors TabFilament::on_value_change().
    if (opt_key == "enable_manual_fan_speeds" && config.has("cooling") &&
        config.opt_bool("enable_manual_fan_speeds", 0) && config.opt_bool("cooling", 0))
    {
        auto *opt = config.option<ConfigOptionBools>("cooling", true);
        if (opt && !opt->values.empty())
            opt->values[0] = false;
        auto cooling_it = m_setting_controls.find("cooling");
        if (cooling_it != m_setting_controls.end())
        {
            if (auto *cb = dynamic_cast<::CheckBox *>(cooling_it->second.control))
                cb->SetValue(false);
            UpdateUndoUI("cooling");
        }
    }
    else if (opt_key == "cooling" && config.has("enable_manual_fan_speeds") && config.opt_bool("cooling", 0) &&
             config.opt_bool("enable_manual_fan_speeds", 0))
    {
        auto *opt = config.option<ConfigOptionBools>("enable_manual_fan_speeds", true);
        if (opt && !opt->values.empty())
            opt->values[0] = false;
        auto manual_it = m_setting_controls.find("enable_manual_fan_speeds");
        if (manual_it != m_setting_controls.end())
        {
            if (auto *cb = dynamic_cast<::CheckBox *>(manual_it->second.control))
                cb->SetValue(false);
            UpdateUndoUI("enable_manual_fan_speeds");
        }
    }

    // Keep legacy MVS alias in sync when MVF changes
    if (opt_key == "filament_max_volumetric_flow")
    {
        auto *flow = config.option<ConfigOptionFloats>("filament_max_volumetric_flow");
        auto *speed = config.option<ConfigOptionFloats>("filament_max_volumetric_speed");
        if (flow && speed)
            speed->values = flow->values;
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
        // update() enables and disables the fields that depend on the changed one and
        // refreshes the cooling and volumetric speed description lines
        tab->update();
    }

    if (GetPlater())
    {
        GetPlater()->on_config_change(config);
    }

    // Apply toggle logic to enable/disable dependent options
    ApplyToggleRules();

    // Update override toggle states (e.g. ramping lift enables max_lift, travel_slope, etc.
    // and retract_length > 0 enables retract_speed, deretract_speed, etc.)
    UpdateOverridesToggleState();
    // Note: m_disable_update is reset by DisableUpdateGuard destructor
}

void FilamentSettingsPanel::UpdateUndoUI(const std::string &opt_key)
{
    auto it = m_setting_controls.find(opt_key);
    if (it != m_setting_controls.end())
        UpdateUndoUICommon(opt_key, it->second.undo_icon, it->second.lock_icon, it->second.original_value);
}

void FilamentSettingsPanel::RefreshFromConfig()
{
    // If we're already inside OnSettingChanged, don't refresh - this prevents the
    // circular callback: OnSettingChanged -> tab->update_dirty() -> RefreshFromConfig()
    // from overwriting the user's in-progress edits with stale config values.
    if (m_disable_update)
        return;

    // RAII guard: sets m_disable_update=true now, restores on scope exit (even if exception thrown)
    DisableUpdateGuard guard(m_disable_update);

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
    ApplyToggleRules();

    // Call SysColorsChanged on parent controls (TextInput, SpinInput, ComboBox)
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
    // Note: m_disable_update is reset by DisableUpdateGuard destructor
}

void FilamentSettingsPanel::ResetOriginalValues()
{
    // Use the saved (non-edited) preset as the undo baseline, matching the Tab system's
    // behavior.  get_edited_preset() contains in-flight modifications and would cause
    // the undo icon to never appear.
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->filaments.get_selected_preset().config;
    for (auto &[opt_key, ui_elem] : m_setting_controls)
    {
        if (config.has(opt_key))
            ui_elem.original_value = config.opt_serialize(opt_key);
    }
}

void FilamentSettingsPanel::ApplyToggleState(const std::string &registry_key, bool enabled, const wxString &reason)
{
    ApplyToggleStateTo(m_setting_controls, registry_key, enabled, reason);
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

void FilamentSettingsPanel::UpdateRowVisibility()
{
    for (auto &[key, ui] : m_setting_controls)
    {
        if (ui.row_sizer && ui.parent_sizer)
        {
            bool vis = is_sidebar_key_visible(key);
            ui.parent_sizer->Show(ui.row_sizer, vis);
        }
    }
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

    // Settings panel with all categories in a continuous scrollable list
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

void ProcessSection::ResetOriginalValues()
{
    if (m_settings_panel)
        m_settings_panel->ResetOriginalValues();
}

void ProcessSection::RebuildContent()
{
    if (m_settings_panel)
        m_settings_panel->RebuildContent();
}

void ProcessSection::UpdateSidebarVisibility()
{
    if (m_settings_panel)
        m_settings_panel->UpdateSidebarVisibility();
}

void ProcessSection::msw_rescale()
{
    if (m_preset_combo)
        m_preset_combo->msw_rescale();
    if (m_settings_panel)
        m_settings_panel->msw_rescale();
}

void ProcessSection::sys_color_changed()
{
    if (m_settings_panel)
        m_settings_panel->sys_color_changed();
}

void ProcessSection::ReapplyTitleAccents()
{
    if (m_settings_panel)
        m_settings_panel->ReapplyTitleAccents();
}

// ============================================================================
// Sidebar Implementation
// ============================================================================

// ============================================================================
// SidebarTabBar - Horizontal tab strip for sidebar navigation
// ============================================================================

class SidebarTabBar : public wxPanel
{
public:
    struct TabItem
    {
        wxString label;
        std::string icon_name;
        wxBitmapBundle icon_bundle;
    };

    SidebarTabBar(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        // Define the 4 tabs with their icons (same icons used by CollapsibleSection headers)
        // Objects first: lightest to render, so startup is fast
        m_tabs = {
            {_L("Objects"), "shape_gallery", {}},
            {_L("Print"), "cog", {}},
            {_L("Filament"), "spool", {}},
            {_L("Printer"), "printer", {}},
        };

        // Load icon bundles
        for (auto &tab : m_tabs)
            tab.icon_bundle = *get_bmp_bundle(tab.icon_name);

        // Calculate height: compact style, DPI-aware
        int em = wxGetApp().em_unit();
        int bar_height = em * 28 / 10; // 2.8em - compact but readable
        SetMinSize(wxSize(-1, bar_height));

        Bind(wxEVT_PAINT, &SidebarTabBar::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &SidebarTabBar::OnMouseDown, this);
        Bind(wxEVT_MOTION, &SidebarTabBar::OnMouseMove, this);
        Bind(wxEVT_LEAVE_WINDOW, &SidebarTabBar::OnMouseLeave, this);
    }

    void SetActiveTab(int index)
    {
        if (index >= 0 && index < (int) m_tabs.size() && index != m_active_tab)
        {
            m_active_tab = index;
            Refresh();
            if (m_on_tab_changed)
                m_on_tab_changed(index);
        }
    }

    int GetActiveTab() const { return m_active_tab; }

    void SetOnTabChanged(std::function<void(int)> cb) { m_on_tab_changed = std::move(cb); }

    // Called on theme/DPI change
    void UpdateAppearance()
    {
        for (auto &tab : m_tabs)
            tab.icon_bundle = *get_bmp_bundle(tab.icon_name);

        int em = wxGetApp().em_unit();
        int bar_height = em * 28 / 10;
        SetMinSize(wxSize(-1, bar_height));
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        wxSize size = GetClientSize();
        int em = wxGetApp().em_unit();
        bool is_dark = wxGetApp().dark_mode();

        // Background - use section header background for visual weight
        wxColour bg_color = is_dark ? UIColors::SectionHeaderBackgroundDark()
                                    : UIColors::SectionHeaderBackgroundLight();
        dc.SetBackground(wxBrush(bg_color));
        dc.Clear();

        if (m_tabs.empty())
            return;

        int tab_count = (int) m_tabs.size();
        int tab_width = size.GetWidth() / tab_count;
        int icon_size = em * 16 / 10; // 1.6em logical icon size
        int padding = em * 6 / 10;    // 0.6em padding

        // Colors
        wxColour text_normal = is_dark ? UIColors::TabTextNormalDark() : UIColors::TabTextNormalLight();
        wxColour text_selected = is_dark ? UIColors::TabTextSelectedDark() : UIColors::TabTextSelectedLight();
        wxColour bg_selected = is_dark ? UIColors::TabBackgroundSelectedDark() : UIColors::TabBackgroundSelectedLight();
        wxColour bg_hover = is_dark ? UIColors::TabBackgroundHoverDark() : UIColors::TabBackgroundHoverLight();
        wxColour divider_color = is_dark ? UIColors::HeaderDividerDark() : UIColors::HeaderDividerLight();
        wxColour accent_color = UIColors::AccentPrimary(); // preFlight orange for active indicator

        // Font - use bold font to match CollapsibleSection accordion headers
        wxFont font = wxGetApp().bold_font();
        dc.SetFont(font);

        for (int i = 0; i < tab_count; i++)
        {
            int x = i * tab_width;
            int w = (i == tab_count - 1) ? (size.GetWidth() - x) : tab_width; // Last tab gets remaining width
            wxRect tab_rect(x, 0, w, size.GetHeight());

            // Draw tab background
            if (i == m_active_tab)
            {
                dc.SetBrush(wxBrush(bg_selected));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRectangle(tab_rect);

                // Active indicator - orange line at bottom (3px)
                int indicator_height = em * 3 / 10;
                if (indicator_height < 2)
                    indicator_height = 2;
                dc.SetBrush(wxBrush(accent_color));
                dc.DrawRectangle(x + 1, size.GetHeight() - indicator_height, w - 2, indicator_height);
            }
            else if (i == m_hovered_tab)
            {
                dc.SetBrush(wxBrush(bg_hover));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawRectangle(tab_rect);
            }

            // Draw icon + text centered in tab
            const auto &tab = m_tabs[i];
            wxBitmap icon = tab.icon_bundle.GetBitmapFor(this);

            // Get icon logical size (GetSize returns physical pixels on Retina)
#ifdef __APPLE__
            wxSize icon_sz = icon.IsOk() ? icon.GetLogicalSize() : wxSize(0, 0);
#else
            wxSize icon_sz = icon.IsOk() ? icon.GetSize() : wxSize(0, 0);
#endif

            wxSize text_sz = dc.GetTextExtent(tab.label);
            int content_width = (icon.IsOk() ? icon_sz.GetWidth() + padding : 0) + text_sz.GetWidth();
            int content_x = x + (w - content_width) / 2;
            int center_y = (size.GetHeight()) / 2;

            // Draw icon
            if (icon.IsOk())
            {
                int icon_y = center_y - icon_sz.GetHeight() / 2;
                dc.DrawBitmap(icon, content_x, icon_y, true);
                content_x += icon_sz.GetWidth() + padding;
            }

            // Draw text
            dc.SetTextForeground(i == m_active_tab ? text_selected : text_normal);
            int text_y = center_y - text_sz.GetHeight() / 2;
            dc.DrawText(tab.label, content_x, text_y);

            // Draw divider between tabs (not after last tab)
            if (i < tab_count - 1)
            {
                int divider_x = x + w - 1;
                int divider_margin = size.GetHeight() / 4; // Vertical margin for divider
                dc.SetPen(wxPen(divider_color, 1));
                dc.DrawLine(divider_x, divider_margin, divider_x, size.GetHeight() - divider_margin);
            }
        }

        // Bottom border line
        dc.SetPen(wxPen(divider_color, 1));
        dc.DrawLine(0, size.GetHeight() - 1, size.GetWidth(), size.GetHeight() - 1);
    }

    void OnMouseDown(wxMouseEvent &evt)
    {
        int tab = HitTest(evt.GetPosition());
        if (tab >= 0)
            SetActiveTab(tab);
    }

    void OnMouseMove(wxMouseEvent &evt)
    {
        int tab = HitTest(evt.GetPosition());
        if (tab != m_hovered_tab)
        {
            m_hovered_tab = tab;
            SetCursor(tab >= 0 ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
            Refresh();
        }
    }

    void OnMouseLeave(wxMouseEvent &)
    {
        if (m_hovered_tab >= 0)
        {
            m_hovered_tab = -1;
            Refresh();
        }
    }

    int HitTest(const wxPoint &pt) const
    {
        if (m_tabs.empty())
            return -1;
        int tab_width = GetClientSize().GetWidth() / (int) m_tabs.size();
        if (tab_width <= 0)
            return -1;
        int idx = pt.x / tab_width;
        if (idx >= 0 && idx < (int) m_tabs.size())
            return idx;
        return -1;
    }

    std::vector<TabItem> m_tabs;
    int m_active_tab{0};
    int m_hovered_tab{-1};
    std::function<void(int)> m_on_tab_changed;
};

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

Sidebar::~Sidebar() {}

void Sidebar::BuildUI()
{
    int em = wxGetApp().em_unit();

    // Set proper background color using unified accessor
    SetBackgroundColour(SidebarColors::Background());

    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // Read tabbed mode preference
    m_tabbed_mode = wxGetApp().app_config->get_bool("use_tabbed_sidebar");

    // Tab bar - horizontal navigation strip
    m_tab_bar = new SidebarTabBar(this);
    m_tab_bar->SetOnTabChanged([this](int /*tab_index*/) { ApplyTabVisibility(); });
    m_main_sizer->Add(m_tab_bar, 0, wxEXPAND);

    // Scrolled panel for sections
    m_scrolled_panel = new wxScrolledWindow(this);
    m_scrolled_panel->SetScrollRate(0, 5);
    m_scrolled_panel->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);

    SetFont(wxGetApp().normal_font());
#ifdef _WIN32
    m_scrolled_panel->SetDoubleBuffered(true);
    // Dark mode theming deferred to CallAfter: HWNDs aren't valid during construction
#endif

    auto *scroll_sizer = new wxBoxSizer(wxVERTICAL);

    // Create collapsible sections
    CreatePrinterSection();
    CreateFilamentSection();
    CreateProcessSection();
    CreateObjectsSection();

    // Order: Print Settings, Filament Settings, Printer Settings, Object Settings
    scroll_sizer->Add(m_process_section, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);
    scroll_sizer->Add(m_filament_section, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);
    scroll_sizer->Add(m_printer_section, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);

    // Object Settings gets proportion 1 to fill remaining space (ObjectList expands)
    scroll_sizer->Add(m_objects_section, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, em / 4);

    m_scrolled_panel->SetSizer(scroll_sizer);
    m_scrolled_panel->FitInside();

    m_main_sizer->Add(m_scrolled_panel, 1, wxEXPAND);

    // Bottom button bar - pinned below the scroll area, shared by both views (never scrolls)
    CreateButtonBar();
    m_main_sizer->Add(m_buttons_panel, 0, wxEXPAND);

    SetSizer(m_main_sizer);

    // Set initial visibility inline: state only, no HWND operations.
    // This prevents the white flash without generating invalid window handle errors.
    if (m_tabbed_mode)
    {
        // Objects tab (0) is default: show process + printer (collapsed pinned only), hide filament
        m_filament_section->Show(false);
        // Sections are created collapsed, so content containers are already hidden.
        // Just ensure objects content is visible.
    }
    else
    {
        m_tab_bar->Show(false);
    }

    // Bind preset combo selection handler - this triggers actual preset changes
    this->Bind(wxEVT_COMBOBOX, &Sidebar::on_select_preset, this);

    // Full visibility setup + layout after window is fully realized (has valid HWNDs)
    CallAfter(
        [this]()
        {
#ifdef _WIN32
            // Deferred dark mode theming, once the HWNDs are valid
            wxGetApp().UpdateDarkUI(this);
            wxGetApp().UpdateDarkUI(m_scrolled_panel);
            if (m_scrolled_panel->GetHWND())
                NppDarkMode::SetDarkExplorerTheme(m_scrolled_panel->GetHWND());
#endif
            // The dark-UI pass above resets every wxStaticText (including the accent headers) to the
            // themed default, so re-assert the accent header colors. Without this they show the
            // dark-mode default (near-white) at startup instead of the accent color.
            if (m_printer_settings_panel)
                m_printer_settings_panel->ReapplyTitleAccents();
            if (m_filament_settings_panel)
                m_filament_settings_panel->ReapplyTitleAccents();
            if (m_process_content)
                m_process_content->ReapplyTitleAccents();
            ApplyTabVisibility();
            // Regenerate the view-mode switch now that HWNDs are valid: its label bitmap
            // is measured via a wxClientDC, which returns zero size during construction.
            if (m_view_mode_switch)
                m_view_mode_switch->Rescale();
            if (m_edit_mode_switch)
                m_edit_mode_switch->Rescale();
            if (m_buttons_panel)
                m_buttons_panel->Layout();
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

void Sidebar::ApplyTabVisibility()
{
    if (!m_tab_bar || !m_process_section || !m_filament_section || !m_printer_section || !m_objects_section)
        return;

    wxSizer *scroll_sizer = m_scrolled_panel->GetSizer();
    if (!scroll_sizer)
        return;

    // Guard: only Freeze/Thaw and Layout when the native window exists.
    // During BuildUI, HWNDs may not exist yet; Show/Hide still sets the wx internal state.
    bool realized = (GetHandle() != nullptr);
    if (realized)
        Freeze();

    // All 4 sections in sizer order (process, filament, printer, objects)
    CollapsibleSection *all_sections[] = {m_process_section, m_filament_section, m_printer_section, m_objects_section};
    bool show_pinned_labels = false;

    if (m_tabbed_mode)
    {
        m_tab_bar->Show();
        int active = m_tab_bar->GetActiveTab();
        // Tab order: 0=Objects, 1=Print, 2=Filament, 3=Printer

        // The sections are frozen only through the Freeze() above; thawing them here would leave
        // their freeze counts one short when the sidebar thaws, and a section whose count wrapped
        // never repaints again.

        // First: hide all headers and collapse every content container while the sections are
        // still shown, then hide the sections. On macOS the native children inside a content
        // container keep their drawing layers in the nearest layer-backed ancestor, the section,
        // and a hide only reaches those layers while the section is visible; a container hidden
        // after its section keeps painting over the pinned rows once the section is shown again.
        for (auto *s : all_sections)
        {
            s->SetHeaderVisible(false);
            if (wxWindow *cc = s->GetContentContainer())
                cc->Show(false);
        }
        for (auto *s : all_sections)
            s->Show(false);

        if (active == 0) // Objects tab: show preset combos + object list
        {
            show_pinned_labels = true;

            // Show Print section collapsed (only pinned combo visible), proportion 0
            m_process_section->Show(true);
            if (wxSizerItem *item = scroll_sizer->GetItem(m_process_section))
                item->SetProportion(0);

            // Show Printer section collapsed (only pinned combo + nozzle/filament rows), proportion 0
            m_printer_section->Show(true);
            if (wxSizerItem *item = scroll_sizer->GetItem(m_printer_section))
                item->SetProportion(0);

            // Show Objects section expanded, proportion 1 to fill remaining space
            m_objects_section->Show(true);
            if (wxWindow *cc = m_objects_section->GetContentContainer())
                cc->Show(true);
            if (wxSizerItem *item = scroll_sizer->GetItem(m_objects_section))
                item->SetProportion(1);
        }
        else
        {
            // Map active tab to section: 1=Print, 2=Filament, 3=Printer
            CollapsibleSection *tab_sections[] = {nullptr, m_process_section, m_filament_section, m_printer_section};
            CollapsibleSection *active_section = tab_sections[active];
            if (active_section)
            {
                active_section->Show(true);
                if (wxWindow *cc = active_section->GetContentContainer())
                    cc->Show(true);
                if (wxSizerItem *item = scroll_sizer->GetItem(active_section))
                    item->SetProportion(1);
            }
        }
    }
    else
    {
        // Unified mode: hide tab bar, show all sections with headers and original proportions
        m_tab_bar->Hide();
        // Sizer order: process(0), filament(0), printer(0), objects(1)
        int proportions[] = {0, 0, 0, 1};
        for (int i = 0; i < 4; i++)
        {
            all_sections[i]->Show();
            all_sections[i]->SetHeaderVisible(true);
            // Restore content container visibility to match expanded state
            if (wxWindow *cc = all_sections[i]->GetContentContainer())
                cc->Show(all_sections[i]->IsExpanded());
            if (wxSizerItem *item = scroll_sizer->GetItem(all_sections[i]))
                item->SetProportion(proportions[i]);
        }
    }

    // Show/hide compact labels and separator for Objects tab in tabbed mode
    if (m_print_pinned_label)
        m_print_pinned_label->Show(show_pinned_labels);
    if (m_printer_pinned_label)
        m_printer_pinned_label->Show(show_pinned_labels);
    if (m_nozzle_pinned_label)
        m_nozzle_pinned_label->Show(show_pinned_labels);
    // Hide the non-bold unified label when the bold tabbed label is shown (and vice versa)
    if (m_nozzle_unified_label)
        m_nozzle_unified_label->Show(!show_pinned_labels);

    // A shown or hidden label changes the pinned block's minimum height, and wx keeps the cached
    // best sizes of every parent until a child invalidates them; without this the sections lay
    // out at the height they had while the labels were hidden and clip the rows below.
    for (wxStaticText *label :
         {m_print_pinned_label, m_printer_pinned_label, m_nozzle_pinned_label, m_nozzle_unified_label})
        if (label)
            label->InvalidateBestSize();

    if (realized)
    {
        m_scrolled_panel->FitInside();
        m_scrolled_panel->Layout();
        Layout();
        Thaw();
    }
}

void Sidebar::CreateButtonBar()
{
    int em = wxGetApp().em_unit();
    // The bar uses the sidebar background so it blends seamlessly with the content above.
    wxColour bar_bg = SidebarColors::Background();

    m_buttons_panel = new wxPanel(this);
    m_buttons_panel->SetBackgroundColour(bar_bg);

    auto *bar_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Both toggles share one fixed width (~40% of the 45em sidebar each) so the two halves stay
    // balanced. Background must be set before SetLabels() so the rendered bitmap blends with the bar.
    // The labeled-track path caps width at GetMaxWidth() (and collapses to empty if left at the -1
    // default); the labels exceed this width, so both pills cap to the same value and render equal.
    const int toggle_w = 20 * em;
    auto make_toggle = [&](const wxString &lbl_off, const wxString &lbl_on, bool value)
    {
        auto *sw = new ::SwitchButton(m_buttons_panel);
        sw->SetBackgroundColour(bar_bg);
        sw->SetMaxSize(wxSize(toggle_w, -1));
        sw->SetLabels(lbl_off, lbl_on);
        sw->SetValue(value);
        return sw;
    };

    // Left half: Tabbed (left) / Accordion (right) view toggle, bound to the use_tabbed_sidebar
    // preference. The switch value is "accordion" (!tabbed) so Tabbed sits on the left.
    m_view_mode_switch = make_toggle(_L("Tabbed View"), _L("Accordion View"), !m_tabbed_mode);
    m_view_mode_switch->SetToolTip(_L("Switch between the accordion and tabbed sidebar layouts"));
    m_view_mode_switch->Bind(wxEVT_TOGGLEBUTTON, &Sidebar::OnViewModeToggle, this);

    // Right half: Pinned Settings (off, normal view) / Edit Visibility (on, editing) toggle
    m_edit_mode_switch = make_toggle(_L("Pinned Settings"), _L("Edit Visibility"), s_sidebar_edit_mode);
    m_edit_mode_switch->SetToolTip(_L("Show all settings with checkboxes to choose which stay pinned to the sidebar"));
    m_edit_mode_switch->Bind(wxEVT_TOGGLEBUTTON, &Sidebar::OnEditModeToggle, this);

    // Two equal halves, each toggle centered within its half
    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);
    left_sizer->AddStretchSpacer(1);
    left_sizer->Add(m_view_mode_switch, 0, wxALIGN_CENTER_VERTICAL);
    left_sizer->AddStretchSpacer(1);

    auto *right_sizer = new wxBoxSizer(wxHORIZONTAL);
    right_sizer->AddStretchSpacer(1);
    right_sizer->Add(m_edit_mode_switch, 0, wxALIGN_CENTER_VERTICAL);
    right_sizer->AddStretchSpacer(1);

    bar_sizer->Add(left_sizer, 1, wxEXPAND | wxTOP | wxBOTTOM, em / 2);
    bar_sizer->Add(right_sizer, 1, wxEXPAND | wxTOP | wxBOTTOM, em / 2);

    m_buttons_panel->SetSizer(bar_sizer);
}

void Sidebar::OnViewModeToggle(wxCommandEvent &evt)
{
    const bool tabbed = !m_view_mode_switch->GetValue(); // switch value is "accordion" (right side)
    wxGetApp().app_config->set("use_tabbed_sidebar", tabbed ? "1" : "0");
    wxGetApp().app_config->save();
    SetTabbedMode(tabbed);
    evt.Skip();
}

void Sidebar::OnEditModeToggle(wxCommandEvent &evt)
{
    // Edit mode reveals every setting (with pin checkboxes); normal view shows only pinned settings.
    s_sidebar_edit_mode = m_edit_mode_switch->GetValue();
    update_sidebar_visibility();
    evt.Skip();
}

void Sidebar::SetTabbedMode(bool tabbed)
{
    // Keep the bottom-bar toggle in sync when the mode is changed elsewhere (e.g. Preferences).
    // The switch value represents "accordion" (the right-side option), i.e. the inverse of tabbed.
    if (m_view_mode_switch && m_view_mode_switch->GetValue() == tabbed)
        m_view_mode_switch->SetValue(!tabbed);
    if (m_tabbed_mode == tabbed)
        return;
    m_tabbed_mode = tabbed;
    ApplyTabVisibility();
}

void Sidebar::BindDeadSpaceHandlers(wxWindow *root)
{
    if (!root)
        return;

    // Recursively bind to all container panels and deadspace widgets (but not input controls)
    std::function<void(wxWindow *)> bind_handler = [this, &bind_handler](wxWindow *win)
    {
        // Bind to container types (wxPanel, wxScrolledWindow) and also to "deadspace" widgets
        // that cover visual area but aren't input controls. In the sidebar, group interiors are
        // FlatStaticBox (wxStaticBox), labels are wxStaticText, and icons are wxStaticBitmap.
        // Unlike the main Tab where OG_CustomCtrl (wxPanel) covers the entire group area,
        // the sidebar's FlatStaticBox inherits from wxStaticBox -> wxControl -> wxWindow (not wxPanel),
        // so without this, clicks between rows within a group would go unhandled.
        bool isContainer = win->IsKindOf(CLASSINFO(wxPanel)) || win->IsKindOf(CLASSINFO(wxScrolledWindow));
        bool isDeadSpace = win->IsKindOf(CLASSINFO(wxStaticBox)) || win->IsKindOf(CLASSINFO(wxStaticText)) ||
                           win->IsKindOf(CLASSINFO(wxStaticBitmap));

        if (isContainer || isDeadSpace)
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

    // Compact label, shown only on the Objects tab in tabbed mode
    m_printer_pinned_label = new wxStaticText(m_printer_content, wxID_ANY, _L("Printer:"));
    m_printer_pinned_label->SetFont(wxGetApp().bold_font());
    m_printer_pinned_label->SetForegroundColour(SidebarColors::Foreground());
    m_printer_pinned_label->Hide();
    pinned_sizer->Add(m_printer_pinned_label, 0, wxLEFT | wxTOP, em / 2);

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

    // Compact label for nozzle/filament rows, shown only on the Objects tab in tabbed mode
    m_nozzle_pinned_label = new wxStaticText(m_printer_content, wxID_ANY,
                                             _L("Nozzle diameter / Filament per extruder:"));
    m_nozzle_pinned_label->SetFont(wxGetApp().bold_font());
    m_nozzle_pinned_label->SetForegroundColour(SidebarColors::Foreground());
    m_nozzle_pinned_label->Hide();
    pinned_sizer->Add(m_nozzle_pinned_label, 0, wxLEFT | wxTOP, em / 2);

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

    // Compact label, shown only on the Objects tab in tabbed mode
    m_print_pinned_label = new wxStaticText(pinned_panel, wxID_ANY, _L("Print Settings:"));
    m_print_pinned_label->SetFont(wxGetApp().bold_font());
    m_print_pinned_label->SetForegroundColour(SidebarColors::Foreground());
    m_print_pinned_label->Hide();
    pinned_sizer->Add(m_print_pinned_label, 0, wxLEFT | wxTOP, em / 2);

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
    // UpdateDarkUI is not called on CollapsibleSection or its content_container,
    // because it overrides the themed background colors that CollapsibleSection sets
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

// Parameters by value: callers pass references into the very config objects this function
// mutates and the refresh chain rewrites.
void commit_filament_color_to_preset(std::string preset_name, std::string color_hex)
{
    auto *preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return;

    // respect_active_preset=false: the true saved baseline is needed here. The default would hand
    // back the active preset's working copy, and saving that would silently persist every
    // unrelated unsaved edit along with the color.
    // System presets are refused outright: they are rebuilt from the vendor bundle at startup,
    // so a color written into one would silently revert. No filament preset ships as system.
    Preset *preset = preset_bundle->filaments.find_preset(preset_name, false, false);
    if (!preset || preset->is_default || preset->is_system)
        return;

    Preset &edited = preset_bundle->filaments.get_edited_preset();
    const bool is_active = edited.name == preset->name;

    // Confirming the picker on the unchanged color must not touch the disk or dirty the project.
    if (boost::iequals(preset->config.opt_string("filament_colour", 0u), color_hex) &&
        (!is_active || boost::iequals(edited.config.opt_string("filament_colour", 0u), color_hex)))
        return;

    auto set_color = [&color_hex](Preset &p)
    {
        auto *opt = p.config.option<ConfigOptionStrings>("filament_colour");
        if (opt && !opt->values.empty())
            opt->values[0] = color_hex;
    };
    set_color(*preset);
    if (is_active)
        set_color(edited);

    // Persist user presets in place, through a temp file so a failed write cannot damage the
    // preset. All path handling goes through the nowide-safe helpers (UTF-8 on Windows), and
    // rename_file retries around transient locks from antivirus and indexers. The PID keeps two
    // app instances from sharing a temp file. NEVER call save() on an external preset: its file
    // member is the project (.3mf) path, and writing there would replace the project with an ini
    // dump. External presets persist through the project save instead.
    if (!preset->is_external && !preset->file.empty())
    {
        const std::string tmp_path = preset->file + "." + std::to_string(get_current_pid()) + ".tmp";
        preset->config.save(tmp_path);
        // ConfigBase::save emits one header line plus one line per key and cannot report
        // failure, so the gate is a read-back: full line count and the new color both present.
        bool written = false;
        {
            boost::nowide::ifstream in(tmp_path);
            if (in)
            {
                const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                const size_t lines = size_t(std::count(content.begin(), content.end(), '\n'));
                written = lines == preset->config.keys().size() + 1 && content.find(color_hex) != std::string::npos;
            }
        }
        if (written)
            written = !Luminary::rename_file(tmp_path, preset->file);
        if (!written)
        {
            boost::nowide::remove(tmp_path.c_str());
            MessageDialog dlg(wxGetApp().mainframe,
                              format_wxstr(_L("Failed to write the filament preset file:\n%1%\n\n"
                                              "The color change is kept for this session and will be included "
                                              "in the project save, but the preset file was not updated."),
                                           from_u8(preset->file)),
                              _L("Filament color"), wxOK | wxICON_ERROR);
            dlg.ShowModal();
        }
    }

    // The color lands in the project's embedded config, so the project is modified regardless of
    // which preset received it. For external presets this is also what guarantees persistence.
    if (auto *plater = wxGetApp().plater())
        plater->set_project_dirty();

    // With baseline and working copy in agreement, this clears any stale dirty state on the
    // color. update_dirty() runs on_presets_changed(), which also refreshes the sidebar combos
    // and the filament panel.
    if (auto *tab = wxGetApp().get_tab(Preset::TYPE_FILAMENT))
    {
        tab->reload_config();
        tab->update_dirty();
        tab->update_changed_ui();
    }
    else
        wxGetApp().sidebar().update_presets(Preset::TYPE_FILAMENT);

    if (auto *plater = wxGetApp().plater())
        plater->on_config_change(preset_bundle->full_config());
}

// Open a color picker for the filament assigned to the given extruder. The color is a physical
// property of the filament, so it is committed straight into that filament's preset.
static void open_filament_color_picker(int extr_idx)
{
    auto *preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return;

    const auto &extruders_filaments = preset_bundle->extruders_filaments;
    size_t eidx = (extr_idx >= 0 && extr_idx < static_cast<int>(extruders_filaments.size())) ? extr_idx : 0;
    const Preset *selected = extruders_filaments[eidx].get_selected_preset();
    if (!selected || selected->is_default)
        return;
    // Copied out: the modal dialog below runs a nested event loop, and `selected` points into the
    // preset container, which may reallocate before the dialog returns.
    const std::string preset_name = selected->name;

    // Show the effective color: the working copy may hold an unsaved color edit from the tab.
    const Preset &edited = preset_bundle->filaments.get_edited_preset();
    const Preset &shown = edited.name == preset_name ? edited : *selected;
    std::string cur_color_str = shown.config.opt_string("filament_colour", 0u);
    wxColour current_color(from_u8(cur_color_str));
    if (!current_color.IsOk())
        current_color = *wxWHITE;

    wxColourData data;
    data.SetChooseFull(true);
    data.SetColour(current_color);

    wxColourDialog dlg(wxGetApp().mainframe, &data);
    dlg.CentreOnParent();
    if (dlg.ShowModal() != wxID_OK)
        return;

    wxColour new_color = dlg.GetColourData().GetColour();
    wxString color_str = wxString::Format("#%02X%02X%02X", new_color.Red(), new_color.Green(), new_color.Blue());
    commit_filament_color_to_preset(preset_name, into_u8(color_str));
}

void Sidebar::init_filament_combo(PlaterPresetComboBox **combo, int extr_idx)
{
    *combo = new PlaterPresetComboBox(m_filament_content, Preset::TYPE_FILAMENT);
    (*combo)->SetMinSize(wxSize(1, -1)); // Allow combo to shrink
    (*combo)->set_extruder_idx(extr_idx);
    (*combo)->SetForegroundColour(SidebarColors::Foreground());
    (*combo)->SetOnClickIcon([extr_idx]() { open_filament_color_picker(extr_idx); });
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
    (*combo)->SetOnClickIcon([extr_idx]() { open_filament_color_picker(extr_idx); });

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

    // Original values come from the SAVED presets (not the edited/in-memory version), so undo
    // shows as modified only when the user changed a value from what is saved. The print preset's
    // print_nozzle_diameters govern the printer's nozzle_diameter: they are applied on every
    // preset load without dirtying the printer, so the baseline is the saved print preset where
    // it carries a value and the saved printer preset for the extruders it does not cover.
    std::vector<double> original_nozzle_values;
    const Preset &saved_preset = wxGetApp().preset_bundle->printers.get_selected_preset();
    const auto *saved_nozzles = saved_preset.config.option<ConfigOptionFloats>("nozzle_diameter");
    if (saved_nozzles)
        original_nozzle_values = saved_nozzles->values;
    // Fallback to current values if saved preset doesn't have them
    if (original_nozzle_values.empty() && nozzle_diameter)
        original_nozzle_values = nozzle_diameter->values;
    const auto *saved_print_nozzles =
        wxGetApp().preset_bundle->prints.get_selected_preset().config.option<ConfigOptionFloats>(
            "print_nozzle_diameters");
    if (saved_print_nozzles)
        for (size_t i = 0; i < original_nozzle_values.size() && i < saved_print_nozzles->values.size(); ++i)
            if (saved_print_nozzles->values[i] > 0.)
                original_nozzle_values[i] = saved_print_nozzles->values[i];

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

        // Add header label (non-bold, shown in unified mode; hidden when bold tabbed label is visible)
        m_nozzle_unified_label = new wxStaticText(m_printer_content, wxID_ANY,
                                                  _L("Nozzle diameter / Filament per extruder:"));
        m_nozzle_unified_label->SetForegroundColour(SidebarColors::Foreground());
        // Hide if tabbed mode Objects tab is active (bold label replaces it)
        if (m_tabbed_mode && m_tab_bar && m_tab_bar->GetActiveTab() == 0)
            m_nozzle_unified_label->Hide();
        m_printer_filament_sizer->Add(m_nozzle_unified_label, 0, wxBOTTOM, em / 4);

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
            row_sizer->Add(combo, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);

            auto *btn_save = new ScalableButton(m_printer_content, wxID_ANY, "save");
            btn_save->SetToolTip(_L("Save current settings to preset"));
            btn_save->Bind(wxEVT_BUTTON,
                           [](wxCommandEvent &) { wxGetApp().get_tab(Preset::TYPE_FILAMENT)->save_preset(); });
            row_sizer->Add(btn_save, 0, wxALIGN_CENTER_VERTICAL);

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

    // The rebuilt rows change the block's minimum height; clear the cached best sizes up to the
    // sidebar so the next layout measures the new rows.
    m_printer_content->InvalidateBestSize();
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
    // The plater's per-extruder colour list follows the new count (the extruder filaments were
    // resized before this call); without it a new extruder's swatch stays empty until a filament
    // changes
    if (m_plater != nullptr)
        m_plater->update_filament_colors_in_full_config();
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
        {
            m_printer_settings_panel->ResetOriginalValues();
            m_printer_settings_panel->RefreshFromConfig();
        }
        break;
    case Preset::TYPE_PRINT:
        if (m_combo_print)
            m_combo_print->update();
        if (m_process_content)
        {
            m_process_content->ResetOriginalValues();
            m_process_content->UpdateFromConfig();
        }
        // The nozzle rows take their baseline from the print preset's print_nozzle_diameters.
        UpdatePrinterFilamentCombos();
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
        {
            m_filament_settings_panel->ResetOriginalValues();
            m_filament_settings_panel->RefreshFromConfig();
        }
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

void Sidebar::show_sliced_info_sizer(bool show)
{
    // No-op in this sidebar.
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
    if (!m_object_list || !m_object_settings)
        return;

    // Get the content container's sizer to change proportions
    wxWindow *content_container = m_object_list->GetParent();
    wxSizer *sizer = content_container ? content_container->GetSizer() : nullptr;

    if (settings_visible)
    {
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

void Sidebar::update_sliced_info_sizer()
{
    // No-op in this sidebar.
}

ConfigOptionsGroup *Sidebar::og_freq_chng_params()
{
    // The new sidebar doesn't use FreqChangedParams in the same way
    // Return nullptr for now - callers should handle this gracefully
    return nullptr;
}

wxButton *Sidebar::get_wiping_dialog_button()
{
    // This sidebar has no wiping dialog button.
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
    // This sidebar has no bulk export button.
    return false;
}

bool Sidebar::show_connect_all(bool show)
{
    // This sidebar has no bulk connect button.
    return false;
}

bool Sidebar::show_export_removable_all(bool show)
{
    // This sidebar has no bulk removable export button.
    return false;
}

void Sidebar::enable_bulk_buttons(bool enable)
{
    // No-op in this sidebar.
}

void Sidebar::switch_to_autoslicing_mode()
{
    // No-op in this sidebar.
}

void Sidebar::switch_from_autoslicing_mode()
{
    // No-op in this sidebar.
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
    // No-op in this sidebar.
}

void Sidebar::update_ui_from_settings()
{
    // No-op: a units change does not reach the sidebar fields from here.
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

void Sidebar::LoadSectionStates()
{
    // Fixed defaults: only Object Settings starts expanded.
    m_section_states["Printer"] = false;
    m_section_states["Filament"] = false;
    m_section_states["Print Settings"] = false;
    m_section_states["Object Settings"] = true;
}

void Sidebar::rebuild_settings_panels()
{
    // Freeze the entire sidebar to batch all window operations
    Freeze();

    // Rebuild all settings panels (destroys + recreates)
    if (m_printer_settings_panel)
        m_printer_settings_panel->RebuildContent();

    if (m_filament_settings_panel)
        m_filament_settings_panel->RebuildContent();

    if (m_process_content)
        m_process_content->RebuildContent();

    Layout();

    Thaw();
}

// The three panels answer the registry dump from their own control maps.
template<typename Elements>
static bool lookup_registry_row(const std::map<std::string, Elements> &controls, const std::string &registry_key,
                                TabbedSettingsPanel::RegistryRowInfo &out)
{
    auto it = controls.find(registry_key);
    if (it == controls.end() || it->second.control == nullptr)
        return false;
    const wxWindow *window = it->second.control;
    out.widget_class = into_u8(wxString(window->GetClassInfo()->GetClassName()));
    out.shown = window->IsShown();
    out.enabled = window->IsEnabled();
    out.reason = into_u8(it->second.disabled_reason);
    return true;
}

bool PrintSettingsPanel::LookupRegistryRow(const std::string &registry_key, RegistryRowInfo &out) const
{
    return lookup_registry_row(m_setting_controls, registry_key, out);
}

bool PrinterSettingsPanel::LookupRegistryRow(const std::string &registry_key, RegistryRowInfo &out) const
{
    return lookup_registry_row(m_setting_controls, registry_key, out);
}

bool FilamentSettingsPanel::LookupRegistryRow(const std::string &registry_key, RegistryRowInfo &out) const
{
    return lookup_registry_row(m_setting_controls, registry_key, out);
}

void Sidebar::SetEditVisibilityMode(bool edit)
{
    s_sidebar_edit_mode = edit;
    if (m_edit_mode_switch && m_edit_mode_switch->GetValue() != edit)
        m_edit_mode_switch->SetValue(edit);
    update_sidebar_visibility();
}

void Sidebar::dump_settings_registry(const std::string &dir)
{
    const bool tabbed_before = m_tabbed_mode;
    const bool edit_before = s_sidebar_edit_mode;
    TabbedSettingsPanel *panels[3] = {m_process_content ? m_process_content->settings_panel() : nullptr,
                                      m_filament_settings_panel, m_printer_settings_panel};
    const char *names[3] = {"print", "filament", "printer"};
    for (TabbedSettingsPanel *panel : panels)
        if (panel != nullptr)
            panel->EnsureAllContentBuilt();
    for (bool tabbed : {true, false})
    {
        SetTabbedMode(tabbed);
        for (bool edit : {false, true})
        {
            SetEditVisibilityMode(edit);
            Layout();
            for (size_t i = 0; i < 3; ++i)
                if (panels[i] != nullptr)
                    panels[i]->DumpRegistry(dir + "/sidebar_" + names[i] + (tabbed ? "_tabbed" : "_accordion") +
                                            (edit ? "_edit" : "_pinned") + ".txt");
        }
    }
    SetEditVisibilityMode(edit_before);
    SetTabbedMode(tabbed_before);
}

void Sidebar::update_sidebar_visibility()
{
    Freeze();

    if (m_printer_settings_panel)
        m_printer_settings_panel->UpdateSidebarVisibility();

    if (m_filament_settings_panel)
        m_filament_settings_panel->UpdateSidebarVisibility();

    if (m_process_content)
        m_process_content->UpdateSidebarVisibility();

    Layout();

    Thaw();
}

// Tab -> Sidebar sync. When a value changes in the main Tab,
// refresh the corresponding sidebar panel so controls and undo buttons stay in sync.
void Sidebar::refresh_settings_panel(Preset::Type type, bool reset_original_values)
{
    switch (type)
    {
    case Preset::TYPE_PRINT:
        if (m_process_content)
        {
            if (reset_original_values)
                m_process_content->ResetOriginalValues();
            m_process_content->UpdateFromConfig();
        }
        break;
    case Preset::TYPE_PRINTER:
        if (m_printer_settings_panel)
        {
            if (reset_original_values)
                m_printer_settings_panel->ResetOriginalValues();
            m_printer_settings_panel->RefreshFromConfig();
        }
        break;
    case Preset::TYPE_FILAMENT:
        if (m_filament_settings_panel)
        {
            if (reset_original_values)
                m_filament_settings_panel->ResetOriginalValues();
            m_filament_settings_panel->RefreshFromConfig();
        }
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

    if (m_tab_bar)
        m_tab_bar->UpdateAppearance();

    if (m_view_mode_switch)
        m_view_mode_switch->Rescale();
    if (m_edit_mode_switch)
        m_edit_mode_switch->Rescale();

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
    if (m_object_settings)
        m_object_settings->msw_rescale();
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

    // Rescale sidebar preset combo boxes (text vertical centering depends on DPI)
    if (m_combo_printer)
        m_combo_printer->msw_rescale();
    if (m_combo_print)
        m_combo_print->msw_rescale();
    for (auto *combo : m_combos_filament)
        if (combo)
            combo->msw_rescale();
    for (auto *combo : m_printer_filament_combos)
        if (combo)
            combo->msw_rescale();

    // ScalableButton doesn't have msw_rescale, only sys_color_changed

    Layout();
}

void Sidebar::sys_color_changed()
{
#ifdef _WIN32
    wxWindowUpdateLocker noUpdates(this);
#endif

    if (m_tab_bar)
        m_tab_bar->UpdateAppearance();

    // Re-theme the bottom button bar, its divider, and both toggles
    if (m_buttons_panel)
    {
        wxColour bar_bg = SidebarColors::Background();
        m_buttons_panel->SetBackgroundColour(bar_bg);
        if (m_view_mode_switch)
        {
            m_view_mode_switch->SetBackgroundColour(bar_bg);
            m_view_mode_switch->Rescale(); // regenerate label bitmaps for the new theme background
        }
        if (m_edit_mode_switch)
        {
            m_edit_mode_switch->SetBackgroundColour(bar_bg);
            m_edit_mode_switch->Rescale();
        }
    }

    // Use unified color accessor - no dark_mode() check needed
    wxColour bg_color = SidebarColors::Background();
    SetBackgroundColour(bg_color);

    if (m_scrolled_panel)
    {
        m_scrolled_panel->SetBackgroundColour(bg_color);
#ifdef _WIN32
        // Always apply DarkMode_Explorer for scrollbar theming
        NppDarkMode::SetDarkExplorerTheme(m_scrolled_panel->GetHWND());
#endif
    }

#ifdef _WIN32
    // Always apply DarkMode_Explorer for scrollbar theming
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
    if (m_object_settings)
        m_object_settings->sys_color_changed();
    if (m_object_layers)
        m_object_layers->sys_color_changed();

    Refresh();
}

} // namespace DSKY
