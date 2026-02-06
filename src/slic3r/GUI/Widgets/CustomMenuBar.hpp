///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_CustomMenuBar_hpp_
#define slic3r_GUI_CustomMenuBar_hpp_

#include <wx/panel.h>
#include <wx/sizer.h>
#include <memory>
#include <vector>
#include <functional>

#include "CustomMenu.hpp"

namespace Slic3r
{
namespace GUI
{

// ============================================================================
// CustomMenuBarItem - A single menu bar item (e.g., "File", "Edit")
// ============================================================================

class CustomMenuBarItem : public wxPanel
{
public:
    CustomMenuBarItem(wxWindow *parent, const wxString &label, std::shared_ptr<CustomMenu> menu);

    void SetMenu(std::shared_ptr<CustomMenu> menu) { m_menu = menu; }
    std::shared_ptr<CustomMenu> GetMenu() const { return m_menu; }

    const wxString &GetMenuLabel() const { return m_label; }
    wxChar GetAccelerator() const { return m_accelerator; }

    void SetSelected(bool selected);
    bool IsSelected() const { return m_selected; }

    // Called by parent when menu mode is active and mouse enters
    void OnMenuModeEnter();

protected:
    void OnPaint(wxPaintEvent &evt);
    void OnMouseEnter(wxMouseEvent &evt);
    void OnMouseLeave(wxMouseEvent &evt);
    void OnLeftDown(wxMouseEvent &evt);
    void OnLeftUp(wxMouseEvent &evt);

private:
    void ShowMenu();
    void HideMenu();

    wxString m_label;
    wxString m_displayLabel; // Without &
    wxChar m_accelerator{0};
    std::shared_ptr<CustomMenu> m_menu;
    bool m_hovered{false};
    bool m_selected{false};
    bool m_pressed{false};

    wxDECLARE_EVENT_TABLE();
};

// ============================================================================
// SwitchingMenusGuard - RAII guard for m_switchingMenus flag
// ============================================================================

class CustomMenuBar; // Forward declaration

class SwitchingMenusGuard
{
public:
    explicit SwitchingMenusGuard(CustomMenuBar *bar);
    ~SwitchingMenusGuard();
    SwitchingMenusGuard(const SwitchingMenusGuard &) = delete;
    SwitchingMenusGuard &operator=(const SwitchingMenusGuard &) = delete;

private:
    CustomMenuBar *m_bar;
};

// ============================================================================
// CustomMenuBar - Replaces wxMenuBar with fully custom themed menus
// ============================================================================

class CustomMenuBar : public wxPanel
{
public:
    CustomMenuBar(wxWindow *parent);

    // Add a menu to the bar
    // label: The display label (e.g., "&File", "&Edit") - & marks accelerator key
    // menu: The wxMenu to convert and show (or nullptr to add later)
    void Append(wxMenu *menu, const wxString &label);

    // Add a pre-built CustomMenu
    void Append(std::shared_ptr<CustomMenu> menu, const wxString &label);

    // Get a menu by index
    std::shared_ptr<CustomMenu> GetMenu(size_t index) const;

    // Get number of menus
    size_t GetMenuCount() const { return m_items.size(); }

    // Enable/disable a top-level menu
    void EnableTop(size_t pos, bool enable);

    // Update colors when theme changes
    void UpdateColors();

    // Update sizes when DPI changes
    void msw_rescale();

    // Handle Alt+key accelerators
    bool HandleAccelerator(wxChar key);

    // Menu mode management
    void EnterMenuMode(CustomMenuBarItem *triggerItem);
    void ExitMenuMode();
    bool IsInMenuMode() const { return m_inMenuMode; }

    // Called when a CustomMenu is dismissed
    void OnMenuDismissed();

    // Set the event handler for menu events
    void SetEventHandler(wxWindow *handler) { m_eventHandler = handler; }
    wxWindow *GetEventHandler() const { return m_eventHandler; }

    // Menu switching flag (to suppress OnMenuDismissed during switching)
    void SetSwitchingMenus(bool switching) { m_switchingMenus = switching; }
    bool IsSwitchingMenus() const { return m_switchingMenus; }

protected:
    void OnPaint(wxPaintEvent &evt);
    void OnKeyDown(wxKeyEvent &evt);

private:
    std::vector<CustomMenuBarItem *> m_items;
    wxBoxSizer *m_sizer;
    wxWindow *m_eventHandler{nullptr};
    bool m_inMenuMode{false};
    bool m_switchingMenus{false}; // Suppress OnMenuDismissed during switching
    CustomMenuBarItem *m_activeItem{nullptr};

    wxDECLARE_EVENT_TABLE();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_CustomMenuBar_hpp_
