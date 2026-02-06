///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_CustomMenu_hpp_
#define slic3r_GUI_CustomMenu_hpp_

#include <wx/popupwin.h>
#include <wx/timer.h>
#include <wx/eventfilter.h>
#include <vector>
#include <functional>
#include <memory>
#include <set>

#include "../wxExtensions.hpp"

namespace Slic3r
{
namespace GUI
{

class CustomMenu;

// ============================================================================
// CustomMenuMouseFilter - Dismisses menus on clicks outside menu hierarchy
// ============================================================================

class CustomMenuMouseFilter : public wxEventFilter
{
public:
    static void Install();
    static void Uninstall();

    int FilterEvent(wxEvent &event) override;

private:
    CustomMenuMouseFilter() = default;
    static CustomMenuMouseFilter *s_instance;
    static int s_refCount;
};

// ============================================================================
// CustomMenuItem - Represents a single menu item
// ============================================================================

struct CustomMenuItem
{
    int id{wxID_ANY};
    wxString label;
    wxString shortcut;     // Extracted from label (after \t)
    wxString displayLabel; // Label without shortcut and with & removed for display
    wxChar accelerator{0}; // The character after & for keyboard navigation
    wxBitmapBundle icon;
    bool enabled{true};
    bool checked{false};
    bool checkable{false};
    bool isSeparator{false};
    std::shared_ptr<CustomMenu> submenu;
    std::function<void()> callback;

    // Construct a regular menu item
    CustomMenuItem(int id, const wxString &label, const wxBitmapBundle &icon = wxBitmapBundle(), bool enabled = true,
                   bool checkable = false, bool checked = false);

    // Construct a separator
    static CustomMenuItem Separator();

    // Construct a submenu item
    CustomMenuItem(int id, const wxString &label, std::shared_ptr<CustomMenu> submenu,
                   const wxBitmapBundle &icon = wxBitmapBundle());

private:
    void parseLabel(const wxString &label);
};

// ============================================================================
// CustomMenu - Custom-drawn popup menu with full theming support
// ============================================================================

class CustomMenu : public wxPopupTransientWindow
{
public:
    CustomMenu();
    explicit CustomMenu(wxWindow *parent);
    ~CustomMenu() override;

    void Create(wxWindow *parent);

    // Building the menu
    void Append(int id, const wxString &label, const wxString &help = wxEmptyString, wxItemKind kind = wxITEM_NORMAL);
    void Append(int id, const wxString &label, const wxBitmapBundle &icon, const wxString &help = wxEmptyString,
                wxItemKind kind = wxITEM_NORMAL);
    void AppendSeparator();
    void AppendSubMenu(std::shared_ptr<CustomMenu> submenu, const wxString &label,
                       const wxBitmapBundle &icon = wxBitmapBundle());

    // Set callback for a menu item by ID
    void SetCallback(int id, std::function<void()> callback);

    // Enable/disable items
    void Enable(int id, bool enable);
    bool IsEnabled(int id) const;

    // Check/uncheck items
    void Check(int id, bool check);
    bool IsChecked(int id) const;

    // Show the menu at a position (relative to parent or screen)
    void ShowAt(const wxPoint &pos, wxWindow *parent = nullptr);
    void ShowBelow(wxWindow *anchor); // Show below a control (for menu bar)

    // Convert from wxMenu
    static std::shared_ptr<CustomMenu> FromWxMenu(wxMenu *menu, wxWindow *eventHandler = nullptr);

    // Get the selected item ID after menu closes (-1 if none)
    int GetSelectedId() const { return m_selectedId; }

    // Set a callback to be called when the menu is dismissed
    void SetDismissCallback(std::function<void()> callback) { m_dismissCallback = std::move(callback); }

    // Dismiss any active context menu (call before showing a new one)
    static void DismissActiveContextMenu();

    // Set this menu as the active context menu (will be dismissed when another is shown)
    // For context menus that use KeepAliveUntilDismissed
    void SetAsActiveContextMenu();

    // Set this menu as active with an external shared_ptr (for menu bar menus)
    void SetAsActiveContextMenu(std::shared_ptr<CustomMenu> menuPtr);

    // Keep the menu alive until dismissed (call with shared_from_this for context menus)
    void KeepAliveUntilDismissed(std::shared_ptr<CustomMenu> self) { m_selfRef = std::move(self); }

    // Check if a screen point is inside this menu or any of its open submenus
    bool ContainsPoint(const wxPoint &screenPt) const;

    // Check if the active context menu (or its submenus) contains the point
    static bool ActiveMenuContainsPoint(const wxPoint &screenPt);

    // Handle a click inside the menu hierarchy (called from mouse filter since popup doesn't receive clicks)
    static void HandleClickInMenuHierarchy(const wxPoint &screenPt);

protected:
    void OnDismiss() override;
    bool ProcessLeftDown(wxMouseEvent &event) override;

public:
    // Call during app shutdown to clean up all bound events
    static void CleanupAllMenus();

    // Called by static timer to handle submenu open/close
    void HandleTimerAction(int itemIndex);

private:
    // Event handlers
    void OnPaint(wxPaintEvent &evt);
    void OnMouseMove(wxMouseEvent &evt);
    void OnMouseDown(wxMouseEvent &evt);
    void OnMouseUp(wxMouseEvent &evt);
    void OnMouseLeave(wxMouseEvent &evt);
    void OnKeyDown(wxKeyEvent &evt);
    void OnAppActivate(wxActivateEvent &evt);

    // Rendering
    void Render(wxDC &dc);
    void DrawItem(wxDC &dc, const CustomMenuItem &item, const wxRect &rect, bool isHovered);
    void DrawSeparator(wxDC &dc, const wxRect &rect);
    void DrawSubmenuArrow(wxDC &dc, const wxRect &rect, bool isHovered);
    void DrawCheckmark(wxDC &dc, const wxRect &rect, bool isHovered);

    // Layout
    void CalculateSize();
    int HitTest(const wxPoint &pt) const;
    wxRect GetItemRect(int index) const;

    // Submenu management
    void OpenSubmenu(int itemIndex);
    void CloseSubmenu();
    void CloseAllSubmenus();

    // Selection
    void SelectItem(int index);
    void ActivateItem(int index);
    void HandleAccelerator(wxChar key);

    // Helper to get item by ID
    CustomMenuItem *FindItemById(int id);
    const CustomMenuItem *FindItemById(int id) const;

    // Data
    std::vector<CustomMenuItem> m_items;
    int m_hoverIndex{-1};
    int m_selectedId{-1};
    wxWindow *m_eventHandler{nullptr};

    // Layout metrics (base values at 100% DPI)
    int m_itemHeight{0};
    int m_separatorHeight{0};
    int m_iconWidth{0};
    int m_shortcutWidth{0};
    int m_totalWidth{0};
    int m_totalHeight{0};
    int m_padding{4}; // Reduced from 8 to match native Windows menus
    int m_iconPadding{4};
    int m_cornerRadius{8};

    // DPI-scaled values (computed in CalculateSize, used in drawing)
    double m_dpiScale{1.0};
    int m_scaledPadding{4};
    int m_scaledIconPadding{4};
    int m_scaledCornerRadius{8};
    int m_scaledIconSize{20};     // Base icon size scaled for DPI
    int m_scaledIndent{10};       // Indent when no icons
    int m_scaledShortcutGap{20};  // Gap before shortcut
    int m_scaledSubmenuArrow{20}; // Submenu arrow space
    int m_scaledSmallGap{5};      // Small gap after content
    int m_scaledMinWidth{160};    // Minimum menu width
    int m_scaledArrowSize{6};     // Submenu arrow size
    int m_scaledCheckSize{10};    // Checkmark size
    int m_scaledHoverDeflateX{4}; // Hover rect deflate X
    int m_scaledHoverDeflateY{1}; // Hover rect deflate Y
    int m_scaledHoverRadius{4};   // Hover rect corner radius
    int m_scaledSubmenuGap{4};    // Gap when opening submenu

    // Submenu handling
    std::shared_ptr<CustomMenu> m_openSubmenu;
    int m_submenuItemIndex{-1};
    int m_pendingSubmenuIndex{-1};
    bool m_submenuClickLock{false}; // Prevents close timer after clicking on submenu item
    static constexpr int SUBMENU_DELAY_MS = 250;

    // Static timer - NOT owned by any window to avoid destruction issues
    static void StartSubmenuTimer(CustomMenu *menu, int itemIndex);
    static void StopSubmenuTimer();

    // Parent menu (for submenu chain)
    CustomMenu *m_parentMenu{nullptr};

    // Dismiss callback (for async notification when menu closes)
    std::function<void()> m_dismissCallback;

    // Self-reference to keep menu alive until dismissed (for context menus)
    std::shared_ptr<CustomMenu> m_selfRef;

    // Static: track active context menu so we can dismiss it when showing a new one
    static std::weak_ptr<CustomMenu> s_activeContextMenu;

    // Static: track all menus that have bound to wxTheApp for cleanup during shutdown
    static std::set<CustomMenu *> s_boundMenus;

    // Allow CustomMenuBar and mouse filter to access internals
    friend class CustomMenuBarItem;
    friend class CustomMenuBar;
    friend class CustomMenuMouseFilter;

    wxDECLARE_EVENT_TABLE();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_CustomMenu_hpp_
