///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "CustomMenu.hpp"
#include "CustomMenuBar.hpp"
#include "UIColors.hpp"
#include "../GUI_App.hpp"

#include <wx/dcbuffer.h>
#include <wx/display.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Slic3r
{
namespace GUI
{

// ============================================================================
// CustomMenuItem Implementation
// ============================================================================

CustomMenuItem::CustomMenuItem(int id, const wxString &label, const wxBitmapBundle &icon, bool enabled, bool checkable,
                               bool checked)
    : id(id), icon(icon), enabled(enabled), checked(checked), checkable(checkable), isSeparator(false)
{
    parseLabel(label);
}

CustomMenuItem CustomMenuItem::Separator()
{
    CustomMenuItem item(wxID_SEPARATOR, wxEmptyString);
    item.isSeparator = true;
    return item;
}

CustomMenuItem::CustomMenuItem(int id, const wxString &label, std::shared_ptr<CustomMenu> submenu,
                               const wxBitmapBundle &icon)
    : id(id), icon(icon), enabled(true), checked(false), checkable(false), isSeparator(false), submenu(submenu)
{
    parseLabel(label);
}

void CustomMenuItem::parseLabel(const wxString &labelText)
{
    label = labelText;

    // Extract shortcut (after \t)
    int tabPos = label.Find('\t');
    if (tabPos != wxNOT_FOUND)
    {
        shortcut = label.Mid(tabPos + 1);
        displayLabel = label.Left(tabPos);
    }
    else
    {
        displayLabel = label;
    }

    // Find accelerator key (character after &)
    int ampPos = displayLabel.Find('&');
    if (ampPos != wxNOT_FOUND && ampPos + 1 < (int) displayLabel.Length())
    {
        accelerator = wxToupper(displayLabel[ampPos + 1]);
    }

    // Remove & from display label
    displayLabel.Replace("&", "");
}

// ============================================================================
// CustomMenu Implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(CustomMenu, wxPopupTransientWindow) EVT_PAINT(CustomMenu::OnPaint)
    EVT_MOTION(CustomMenu::OnMouseMove) EVT_LEFT_DOWN(CustomMenu::OnMouseDown) EVT_LEFT_UP(CustomMenu::OnMouseUp)
        EVT_LEAVE_WINDOW(CustomMenu::OnMouseLeave) EVT_KEY_DOWN(CustomMenu::OnKeyDown) wxEND_EVENT_TABLE()

    // Static members
    std::weak_ptr<CustomMenu> CustomMenu::s_activeContextMenu;
std::set<CustomMenu *> CustomMenu::s_boundMenus;

// ============================================================================
// Static Submenu Timer - NOT owned by any window to avoid destruction issues
// ============================================================================

// Forward declaration for timer callback
static void HandleSubmenuTimerCallback(CustomMenu *menu, int itemIndex);

namespace
{
class SubmenuTimer : public wxTimer
{
public:
    void SetTarget(CustomMenu *menu, int itemIndex)
    {
        m_menu = menu;
        m_itemIndex = itemIndex;
    }

    void ClearTarget()
    {
        m_menu = nullptr;
        m_itemIndex = -1;
    }

    void Notify() override
    {
        if (!wxTheApp || !wxTheApp->IsMainLoopRunning())
        {
            Stop();
            ClearTarget();
            return;
        }

        CustomMenu *menu = m_menu;
        int itemIndex = m_itemIndex;
        ClearTarget();

        // Call the static handler function (defined after CustomMenu class)
        HandleSubmenuTimerCallback(menu, itemIndex);
    }

private:
    CustomMenu *m_menu{nullptr};
    int m_itemIndex{-1};
};

SubmenuTimer *GetSubmenuTimer()
{
    static SubmenuTimer timer; // Static lifetime - never destroyed until process exit
    return &timer;
}
} // namespace

// ============================================================================
// CustomMenuMouseFilter implementation
// ============================================================================

CustomMenuMouseFilter *CustomMenuMouseFilter::s_instance = nullptr;
int CustomMenuMouseFilter::s_refCount = 0;

void CustomMenuMouseFilter::Install()
{
    if (s_refCount == 0)
    {
        s_instance = new CustomMenuMouseFilter();
        wxEvtHandler::AddFilter(s_instance);
    }
    s_refCount++;
}

void CustomMenuMouseFilter::Uninstall()
{
    if (s_refCount > 0)
    {
        s_refCount--;
        if (s_refCount == 0 && s_instance)
        {
            wxEvtHandler::RemoveFilter(s_instance);
            delete s_instance;
            s_instance = nullptr;
        }
    }
}

int CustomMenuMouseFilter::FilterEvent(wxEvent &event)
{
    // Only handle left mouse button down events for menu interaction
    wxEventType type = event.GetEventType();
    if (type != wxEVT_LEFT_DOWN && type != wxEVT_RIGHT_DOWN && type != wxEVT_MIDDLE_DOWN)
    {
        return Event_Skip; // Let other handlers process
    }

    // Check if there's an active context menu
    auto activeMenu = CustomMenu::s_activeContextMenu.lock();
    if (!activeMenu || !activeMenu->IsShown())
    {
        return Event_Skip;
    }

    // Get mouse position in screen coordinates
    wxMouseEvent *mouseEvt = dynamic_cast<wxMouseEvent *>(&event);
    if (!mouseEvt)
    {
        return Event_Skip;
    }

    wxPoint screenPt = wxGetMousePosition();

    // Check if click is inside the menu hierarchy
    if (!CustomMenu::ActiveMenuContainsPoint(screenPt))
    {
        // Click is outside - dismiss the menu
        CustomMenu::DismissActiveContextMenu();
        // Don't consume the event - let the canvas handle it
        return Event_Skip;
    }

    // Click is inside menu hierarchy - handle it directly since popup doesn't receive mouse events
    if (type == wxEVT_LEFT_DOWN)
    {
        // Find which menu in the hierarchy contains this point and handle the click
        CustomMenu::HandleClickInMenuHierarchy(screenPt);
        // CRITICAL: Return Event_Processed to prevent wxPopupTransientWindow from dismissing submenus
        return Event_Processed;
    }

    return Event_Skip;
}

// ============================================================================
// CustomMenu static methods
// ============================================================================

void CustomMenu::DismissActiveContextMenu()
{
    if (auto activeMenu = s_activeContextMenu.lock())
    {
        // Close any submenus first
        activeMenu->CloseAllSubmenus();

        // Hide immediately
        activeMenu->Hide();

        // Dismiss will trigger OnDismiss which clears self-ref
        activeMenu->Dismiss();
    }
    s_activeContextMenu.reset();
}

void CustomMenu::SetAsActiveContextMenu()
{
    // Only root menus (no parent) should be set as active context menu
    if (!m_parentMenu && m_selfRef)
    {
        s_activeContextMenu = m_selfRef;
    }
}

void CustomMenu::SetAsActiveContextMenu(std::shared_ptr<CustomMenu> menuPtr)
{
    // For menu bar menus that don't use m_selfRef - track via external shared_ptr
    if (!m_parentMenu && menuPtr)
    {
        s_activeContextMenu = menuPtr;
    }
}

void CustomMenu::StartSubmenuTimer(CustomMenu *menu, int itemIndex)
{
    auto *timer = GetSubmenuTimer();
    timer->Stop();
    timer->SetTarget(menu, itemIndex);
    timer->StartOnce(SUBMENU_DELAY_MS);
}

void CustomMenu::StopSubmenuTimer()
{
    auto *timer = GetSubmenuTimer();
    timer->Stop();
    timer->ClearTarget();
}

void CustomMenu::CleanupAllMenus()
{
    // Stop the static timer
    auto *timer = GetSubmenuTimer();
    timer->Stop();
    timer->ClearTarget();

    // CRITICAL: Remove menus from their parents to prevent double-delete
    // wxWidgets parent-child ownership conflicts with shared_ptr ownership
    // By removing from parent, wxWidgets won't auto-delete via DestroyChildren()
    // The shared_ptr will handle proper deletion
    for (auto *menu : s_boundMenus)
    {
        if (wxWindow *parent = menu->GetParent())
        {
            parent->RemoveChild(menu);
        }

        // Also unbind app events
        if (wxTheApp)
        {
            wxTheApp->Unbind(wxEVT_ACTIVATE_APP, &CustomMenu::OnAppActivate, menu);
        }
    }
    s_boundMenus.clear();

    // Clear active context menu
    s_activeContextMenu.reset();
}

bool CustomMenu::ContainsPoint(const wxPoint &screenPt) const
{
    if (!IsShown())
    {
        return false;
    }

    wxRect menuRect = GetScreenRect();
    if (menuRect.Contains(screenPt))
    {
        return true;
    }

    // Check open submenu recursively
    if (m_openSubmenu && m_openSubmenu->ContainsPoint(screenPt))
    {
        return true;
    }

    return false;
}

bool CustomMenu::ActiveMenuContainsPoint(const wxPoint &screenPt)
{
    if (auto activeMenu = s_activeContextMenu.lock())
    {
        return activeMenu->ContainsPoint(screenPt);
    }
    return false;
}

void CustomMenu::HandleClickInMenuHierarchy(const wxPoint &screenPt)
{
    auto activeMenu = s_activeContextMenu.lock();
    if (!activeMenu)
    {
        return;
    }

    // Find which menu in the hierarchy contains the point
    CustomMenu *targetMenu = nullptr;
    CustomMenu *current = activeMenu.get();

    while (current)
    {
        wxRect menuRect = current->GetScreenRect();
        if (menuRect.Contains(screenPt))
        {
            targetMenu = current;
            break;
        }
        // Check the open submenu
        current = current->m_openSubmenu.get();
    }

    if (!targetMenu)
    {
        return;
    }

    // Convert screen point to local coordinates for hit testing
    wxPoint localPt = targetMenu->ScreenToClient(screenPt);
    int index = targetMenu->HitTest(localPt);
    if (index < 0)
    {
        return;
    }

    // Update hover state
    targetMenu->m_hoverIndex = index;
    targetMenu->Refresh();

    // Handle the click
    if (targetMenu->m_items[index].submenu && targetMenu->m_items[index].enabled)
    {
        // Submenu item - open it immediately
        targetMenu->StopSubmenuTimer();
        targetMenu->m_pendingSubmenuIndex = -1;
        targetMenu->m_submenuClickLock = true; // Prevent close timer after click
        targetMenu->OpenSubmenu(index);
    }
    else if (targetMenu->m_items[index].enabled && !targetMenu->m_items[index].isSeparator)
    {
        // Regular item - activate it
        targetMenu->ActivateItem(index);
    }
}

CustomMenu::CustomMenu()
{
    // Nothing to initialize - static timer is used
}

CustomMenu::CustomMenu(wxWindow *parent)
{
    Create(parent);
}

CustomMenu::~CustomMenu()
{
    // NOTE: Do NOT call m_selfRef.reset() here!
    // If we're being deleted via wxWidgets (delete), calling reset() would cause
    // the shared_ptr to try to delete us again. Let the member be destroyed normally.
    // The fix is in Create() where we use top-level parent to avoid auto-deletion.

    // Remove from bound menus tracking
    s_boundMenus.erase(this);

    // Clear submenu relationships to prevent dangling pointers
    for (auto &item : m_items)
    {
        if (item.submenu)
        {
            item.submenu->m_parentMenu = nullptr;
        }
    }

    if (m_openSubmenu)
    {
        m_openSubmenu->m_parentMenu = nullptr;
        m_openSubmenu.reset();
    }

    // If we were never properly dismissed, clean up the mouse filter
    if (!m_parentMenu)
    {
        auto activeMenu = s_activeContextMenu.lock();
        if (activeMenu.get() == this)
        {
            s_activeContextMenu.reset();
            CustomMenuMouseFilter::Uninstall();
        }
    }
}

void CustomMenu::Create(wxWindow *parent)
{
    // Don't recreate if already created
    if (GetHandle())
        return;

    wxPopupTransientWindow::Create(parent, wxBORDER_NONE | wxPOPUP_WINDOW);
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Explicitly bind mouse events (in addition to event table) for reliable event handling
    Bind(wxEVT_LEFT_DOWN, &CustomMenu::OnMouseDown, this);
    Bind(wxEVT_LEFT_UP, &CustomMenu::OnMouseUp, this);

    // Track this menu for app event cleanup during shutdown
    s_boundMenus.insert(this);

    // Close menu when app loses focus (Windows doesn't do this automatically for popups)
    wxTheApp->Bind(wxEVT_ACTIVATE_APP, &CustomMenu::OnAppActivate, this);
}

void CustomMenu::Append(int id, const wxString &label, const wxString & /*help*/, wxItemKind kind)
{
    bool checkable = (kind == wxITEM_CHECK || kind == wxITEM_RADIO);
    m_items.emplace_back(id, label, wxBitmapBundle(), true, checkable, false);
}

void CustomMenu::Append(int id, const wxString &label, const wxBitmapBundle &icon, const wxString & /*help*/,
                        wxItemKind kind)
{
    bool checkable = (kind == wxITEM_CHECK || kind == wxITEM_RADIO);
    m_items.emplace_back(id, label, icon, true, checkable, false);
}

void CustomMenu::AppendSeparator()
{
    m_items.push_back(CustomMenuItem::Separator());
}

void CustomMenu::AppendSubMenu(std::shared_ptr<CustomMenu> submenu, const wxString &label, const wxBitmapBundle &icon)
{
    m_items.emplace_back(wxID_ANY, label, submenu, icon);
}

void CustomMenu::SetCallback(int id, std::function<void()> callback)
{
    if (auto *item = FindItemById(id))
    {
        item->callback = std::move(callback);
    }
}

void CustomMenu::Enable(int id, bool enable)
{
    if (auto *item = FindItemById(id))
    {
        item->enabled = enable;
    }
}

bool CustomMenu::IsEnabled(int id) const
{
    if (const auto *item = FindItemById(id))
    {
        return item->enabled;
    }
    return false;
}

void CustomMenu::Check(int id, bool check)
{
    if (auto *item = FindItemById(id))
    {
        item->checked = check;
    }
}

bool CustomMenu::IsChecked(int id) const
{
    if (const auto *item = FindItemById(id))
    {
        return item->checked;
    }
    return false;
}

CustomMenuItem *CustomMenu::FindItemById(int id)
{
    for (auto &item : m_items)
    {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

const CustomMenuItem *CustomMenu::FindItemById(int id) const
{
    for (const auto &item : m_items)
    {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

void CustomMenu::CalculateSize()
{
    // Get DPI scale factor for proper high-DPI rendering
    // Try multiple methods to get reliable DPI scale
    m_dpiScale = GetContentScaleFactor();

    // If GetContentScaleFactor returns 1.0, try getting scale from the display
    // This handles cases where popup DPI context isn't established yet
    if (m_dpiScale <= 1.0)
    {
        wxPoint pos = GetPosition();
        int displayIndex = wxDisplay::GetFromPoint(pos);
        if (displayIndex == wxNOT_FOUND)
            displayIndex = wxDisplay::GetFromWindow(GetParent());
        if (displayIndex == wxNOT_FOUND)
            displayIndex = 0;

        if (displayIndex >= 0 && displayIndex < static_cast<int>(wxDisplay::GetCount()))
        {
            wxDisplay display(displayIndex);
            m_dpiScale = display.GetScaleFactor();
        }
    }

    if (m_dpiScale < 1.0)
        m_dpiScale = 1.0; // Safety: never scale down

    // Calculate all DPI-scaled values upfront
    m_scaledPadding = static_cast<int>(m_padding * m_dpiScale);
    m_scaledIconPadding = static_cast<int>(m_iconPadding * m_dpiScale);
    m_scaledCornerRadius = static_cast<int>(m_cornerRadius * m_dpiScale);
    m_scaledIconSize = static_cast<int>(20 * m_dpiScale);
    m_scaledIndent = static_cast<int>(10 * m_dpiScale);
    m_scaledShortcutGap = static_cast<int>(20 * m_dpiScale);
    m_scaledSubmenuArrow = static_cast<int>(20 * m_dpiScale);
    m_scaledSmallGap = static_cast<int>(5 * m_dpiScale);
    m_scaledMinWidth = static_cast<int>(160 * m_dpiScale);
    m_scaledArrowSize = static_cast<int>(6 * m_dpiScale);
    m_scaledCheckSize = static_cast<int>(10 * m_dpiScale);
    m_scaledHoverDeflateX = static_cast<int>(4 * m_dpiScale);
    m_scaledHoverDeflateY = static_cast<int>(1 * m_dpiScale);
    m_scaledHoverRadius = static_cast<int>(4 * m_dpiScale);
    m_scaledSubmenuGap = static_cast<int>(4 * m_dpiScale);

    // Use DC from this window (popup) to get correct DPI-scaled text measurements
    // This works because SetPosition() was called before CalculateSize() to establish DPI context
    wxClientDC dc(this);
    dc.SetFont(GetFont().IsOk() ? GetFont() : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // Item height uses scaled padding (font already scales with DPI)
    m_itemHeight = dc.GetCharHeight() + m_scaledPadding * 2;
    m_separatorHeight = static_cast<int>(9 * m_dpiScale); // 1px line + 4px padding each side
    m_iconWidth = 0;
    m_shortcutWidth = 0;

    int maxLabelWidth = 0;

    // First pass: determine icon presence and max widths
    bool hasIcons = false;
    bool hasCheckable = false;
    bool hasSubmenus = false;

    for (const auto &item : m_items)
    {
        if (item.isSeparator)
            continue;

        if (item.icon.IsOk())
            hasIcons = true;

        if (item.checkable)
            hasCheckable = true;

        if (item.submenu)
            hasSubmenus = true;

        wxSize labelSize = dc.GetTextExtent(item.displayLabel);
        maxLabelWidth = std::max(maxLabelWidth, labelSize.x);

        if (!item.shortcut.empty())
        {
            wxSize shortcutSize = dc.GetTextExtent(item.shortcut);
            m_shortcutWidth = std::max(m_shortcutWidth, shortcutSize.x);
        }
    }

    // Icon column width (includes checkmarks if needed) - scaled
    if (hasIcons || hasCheckable)
    {
        m_iconWidth = m_scaledIconSize + m_scaledIconPadding * 2;
    }

    // Calculate total width using scaled values
    m_totalWidth = m_scaledPadding;                                 // Left padding
    m_totalWidth += m_iconWidth > 0 ? m_iconWidth : m_scaledIndent; // Icon/check column or small indent
    m_totalWidth += maxLabelWidth;                                  // Label (already scaled by font)
    if (m_shortcutWidth > 0)
    {
        m_totalWidth += m_scaledShortcutGap; // Gap before shortcut
        m_totalWidth += m_shortcutWidth;     // Shortcut (already scaled by font)
    }
    if (hasSubmenus)
    {
        m_totalWidth += m_scaledSubmenuArrow; // Submenu arrow space
    }
    m_totalWidth += m_scaledSmallGap; // Small gap after content
    m_totalWidth += m_scaledPadding;  // Right padding

    // Minimum width (scaled)
    m_totalWidth = std::max(m_totalWidth, m_scaledMinWidth);

    // Calculate total height using scaled padding
    m_totalHeight = m_scaledPadding; // Top padding
    for (const auto &item : m_items)
    {
        m_totalHeight += item.isSeparator ? m_separatorHeight : m_itemHeight;
    }
    m_totalHeight += m_scaledPadding; // Bottom padding

    SetSize(m_totalWidth, m_totalHeight);

#ifdef _WIN32
    // Set rounded window region using scaled corner radius
    HWND hwnd = GetHWND();
    if (hwnd)
    {
        HRGN hrgn = CreateRoundRectRgn(0, 0, m_totalWidth + 1, m_totalHeight + 1, m_scaledCornerRadius * 2,
                                       m_scaledCornerRadius * 2);
        SetWindowRgn(hwnd, hrgn, TRUE);
        // Note: Windows takes ownership of the region, don't delete it
    }
#endif
}

void CustomMenu::ShowAt(const wxPoint &pos, wxWindow *parent)
{
    // If this is a root context menu (not a submenu), dismiss any existing active context menu
    if (!m_parentMenu)
    {
        DismissActiveContextMenu();
    }

    if (parent)
    {
        m_eventHandler = parent;
        if (!GetParent())
            Create(parent);
    }

    // CRITICAL: Set position FIRST to establish correct DPI context
    // Under Per-Monitor v2 DPI awareness, the popup's DPI context is determined
    // by its position on screen. Calculating size before positioning can result
    // in measurements based on the wrong DPI.
    SetPosition(pos);

    CalculateSize();

    // Adjust position to stay on screen
    wxPoint finalPos = pos;

    // Validate display - use display from point, or fall back to primary display
    int displayIndex = wxDisplay::GetFromPoint(pos);
    if (displayIndex == wxNOT_FOUND)
    {
        // Invalid position, use primary display
        displayIndex = 0;
    }

    if (displayIndex >= 0 && displayIndex < (int) wxDisplay::GetCount())
    {
        wxDisplay display(displayIndex);
        wxRect screenRect = display.GetClientArea();

        if (finalPos.x + m_totalWidth > screenRect.GetRight())
            finalPos.x = screenRect.GetRight() - m_totalWidth;
        if (finalPos.x < screenRect.GetLeft())
            finalPos.x = screenRect.GetLeft();

        if (finalPos.y + m_totalHeight > screenRect.GetBottom())
            finalPos.y = screenRect.GetBottom() - m_totalHeight;
        if (finalPos.y < screenRect.GetTop())
            finalPos.y = screenRect.GetTop();
    }

    SetPosition(finalPos);
    Popup();
    // If this is a root context menu, register it as active so it can be dismissed later
    if (!m_parentMenu)
    {
        SetAsActiveContextMenu();
        // Install mouse filter to catch clicks outside menu
        CustomMenuMouseFilter::Install();
    }
}

void CustomMenu::ShowBelow(wxWindow *anchor)
{
    if (!anchor || !anchor->GetHandle())
        return;

    wxPoint pos = anchor->ClientToScreen(wxPoint(0, anchor->GetSize().y));

    // Validate position
    if (pos.x < -10000 || pos.x > 100000 || pos.y < -10000 || pos.y > 100000)
        return;

    ShowAt(pos, anchor);
}

std::shared_ptr<CustomMenu> CustomMenu::FromWxMenu(wxMenu *menu, wxWindow *eventHandler)
{
    if (!menu)
        return nullptr;

    // IMPORTANT: Use shared_ptr(new ...) instead of make_shared
    // make_shared embeds object in control block, but wxWidgets calls delete directly
    // on child windows, which corrupts heap when used with make_shared allocation
    auto customMenu = std::shared_ptr<CustomMenu>(new CustomMenu());

    size_t count = menu->GetMenuItemCount();
    for (size_t i = 0; i < count; ++i)
    {
        wxMenuItem *wxItem = menu->FindItemByPosition(i);
        if (!wxItem)
            continue;

        if (wxItem->IsSeparator())
        {
            customMenu->AppendSeparator();
        }
        else if (wxItem->IsSubMenu())
        {
            auto submenu = FromWxMenu(wxItem->GetSubMenu(), eventHandler);
            wxBitmapBundle icon;
            if (wxItem->GetBitmap().IsOk())
                icon = wxBitmapBundle::FromBitmap(wxItem->GetBitmap());

            customMenu->m_items.emplace_back(wxItem->GetId(), wxItem->GetItemLabelText(), submenu, icon);
            customMenu->m_items.back().enabled = wxItem->IsEnabled();
        }
        else
        {
            wxBitmapBundle icon;
            if (wxItem->GetBitmap().IsOk())
                icon = wxBitmapBundle::FromBitmap(wxItem->GetBitmap());

            // Use GetItemLabel() to preserve the full label with shortcut
            customMenu->m_items.emplace_back(wxItem->GetId(), wxItem->GetItemLabel(), icon, wxItem->IsEnabled(),
                                             wxItem->IsCheckable(), wxItem->IsChecked());

            // Capture menu and menu item ID for callback
            // On Windows, callbacks may be bound to parent menu, not submenu, so try multiple dispatch methods
            int itemId = wxItem->GetId();
            wxMenu *wxMenuPtr = menu;
            customMenu->m_items.back().callback = [eventHandler, wxMenuPtr, itemId]()
            {
                wxCommandEvent evt(wxEVT_MENU, itemId);
                evt.SetEventObject(wxMenuPtr);

                // Try multiple ways to dispatch the event (Windows binds to parent menu, not submenu)
                bool handled = false;

                // 1. Try the menu itself
                if (!handled && wxMenuPtr)
                    handled = wxMenuPtr->ProcessEvent(evt);

                // 2. Try walking up the menu hierarchy to find parent menus
                if (!handled)
                {
                    wxMenu *parentMenu = wxMenuPtr ? wxMenuPtr->GetParent() : nullptr;
                    while (parentMenu && !handled)
                    {
                        evt.SetEventObject(parentMenu);
                        handled = parentMenu->ProcessEvent(evt);
                        parentMenu = parentMenu->GetParent();
                    }
                }

                // 3. Try the event handler (e.g., GLCanvas, MainFrame)
                if (!handled && eventHandler)
                {
                    evt.SetEventObject(eventHandler);
                    handled = eventHandler->ProcessWindowEvent(evt);
                }

                // 4. Try the top-level window as last resort
                if (!handled)
                {
                    if (wxWindow *topWindow = wxTheApp->GetTopWindow())
                    {
                        evt.SetEventObject(topWindow);
                        topWindow->ProcessWindowEvent(evt);
                    }
                }
            };
        }
    }

    customMenu->m_eventHandler = eventHandler;
    return customMenu;
}

void CustomMenu::OnDismiss()
{
    // If this is a submenu, tell the parent to clear its submenu tracking
    // This handles the case where wxPopupTransientWindow dismisses us unexpectedly
    if (m_parentMenu && m_parentMenu->m_openSubmenu.get() == this)
    {
        m_parentMenu->m_openSubmenu.reset();
        m_parentMenu->m_submenuItemIndex = -1;
        m_parentMenu->m_submenuClickLock = false;
    }

    CloseSubmenu();
    StopSubmenuTimer();
    m_hoverIndex = -1;

    // Clear from active context menu if this is the root menu
    if (!m_parentMenu)
    {
        s_activeContextMenu.reset();
        // Uninstall mouse filter
        CustomMenuMouseFilter::Uninstall();
    }

    // Check if app is shutting down - avoid CallAfter during shutdown
    bool appShuttingDown = !wxTheApp || !wxTheApp->IsMainLoopRunning();

    // Call the dismiss callback if set (for context menus that need notification)
    if (m_dismissCallback)
    {
        if (!appShuttingDown)
        {
            auto callback = m_dismissCallback;
            wxTheApp->CallAfter([callback]() { callback(); });
        }
        else
        {
            // During shutdown, call directly
            m_dismissCallback();
        }
    }

    // Notify parent CustomMenuBarItem that menu was dismissed
    if (wxWindow *parent = GetParent())
    {
        if (!appShuttingDown)
        {
            parent->CallAfter(
                [parent]()
                {
                    wxWindow *win = parent;
                    while (win)
                    {
                        if (auto *menuBar = dynamic_cast<CustomMenuBar *>(win))
                        {
                            menuBar->OnMenuDismissed();
                            break;
                        }
                        win = win->GetParent();
                    }
                });
        }
        // During shutdown, skip notification - menu bar is being destroyed anyway
    }

    // Clear the self-reference to allow destruction
    if (m_selfRef)
    {
        if (!appShuttingDown)
        {
            auto selfRef = std::move(m_selfRef);
            wxTheApp->CallAfter(
                [selfRef]()
                {
                    // selfRef goes out of scope here, allowing destruction
                });
        }
        else
        {
            // During shutdown, just clear directly
            m_selfRef.reset();
        }
    }

    wxPopupTransientWindow::OnDismiss();
}

void CustomMenu::OnPaint(wxPaintEvent & /*evt*/)
{
    wxAutoBufferedPaintDC dc(this);
    Render(dc);
}

void CustomMenu::Render(wxDC &dc)
{
    wxSize size = GetSize();

    // Get colors from UIColors
    wxColour bgColor = UIColors::MenuBackground();
    wxColour borderColor = UIColors::AccentPrimary(); // preFlight Orange

    // Clear entire window first (handles the margin outside the rounded rect)
    dc.SetBrush(wxBrush(bgColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, size.x, size.y);

    // Draw rounded rectangle border with 1px inset to fit within window region
    int borderInset = std::max(1, static_cast<int>(m_dpiScale));
    dc.SetPen(wxPen(borderColor, borderInset));
    dc.DrawRoundedRectangle(borderInset, borderInset, size.x - borderInset * 2, size.y - borderInset * 2,
                            m_scaledCornerRadius);

    // Draw items
    int y = m_scaledPadding;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const auto &item = m_items[i];
        int itemH = item.isSeparator ? m_separatorHeight : m_itemHeight;
        wxRect rect(0, y, size.x, itemH);

        if (item.isSeparator)
        {
            DrawSeparator(dc, rect);
        }
        else
        {
            bool isHovered = ((int) i == m_hoverIndex);
            DrawItem(dc, item, rect, isHovered);
        }

        y += itemH;
    }
}

void CustomMenu::DrawItem(wxDC &dc, const CustomMenuItem &item, const wxRect &rect, bool isHovered)
{
    wxColour textColor = item.enabled ? UIColors::MenuText() : UIColors::InputForegroundDisabled();
    wxColour hoverBg = UIColors::MenuHover();

    // Draw hover background with scaled deflate and corner radius
    if (isHovered && item.enabled)
    {
        dc.SetBrush(wxBrush(hoverBg));
        dc.SetPen(*wxTRANSPARENT_PEN);
        // Inset hover rect slightly from edges (scaled)
        wxRect hoverRect = rect;
        hoverRect.Deflate(m_scaledHoverDeflateX, m_scaledHoverDeflateY);
        dc.DrawRoundedRectangle(hoverRect, m_scaledHoverRadius);
    }

    int x = rect.x + m_scaledPadding;

    // Draw checkmark or icon
    if (m_iconWidth > 0)
    {
        if (item.checkable && item.checked)
        {
            DrawCheckmark(dc, wxRect(x, rect.y, m_iconWidth, rect.height), isHovered);
        }
        else if (item.icon.IsOk())
        {
            wxBitmap bmp = item.icon.GetBitmapFor(this);
            if (bmp.IsOk())
            {
                int iconY = rect.y + (rect.height - bmp.GetHeight()) / 2;
                dc.DrawBitmap(bmp, x + m_scaledIconPadding, iconY, true);
            }
        }
        x += m_iconWidth;
    }
    else
    {
        // Small indent when no icons present (scaled)
        x += m_scaledIndent;
    }

    // Draw label
    dc.SetTextForeground(textColor);
    dc.SetFont(GetFont().IsOk() ? GetFont() : wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    int textY = rect.y + (rect.height - dc.GetCharHeight()) / 2;
    dc.DrawText(item.displayLabel, x, textY);

    // Draw shortcut (right-aligned) with scaled gap
    if (!item.shortcut.empty())
    {
        wxColour shortcutColor = item.enabled ? UIColors::SecondaryText() : UIColors::InputForegroundDisabled();
        dc.SetTextForeground(shortcutColor);

        int shortcutX = rect.GetRight() - m_scaledPadding - m_scaledShortcutGap - dc.GetTextExtent(item.shortcut).x;
        dc.DrawText(item.shortcut, shortcutX, textY);
    }

    // Draw submenu arrow
    if (item.submenu)
    {
        DrawSubmenuArrow(dc, rect, isHovered && item.enabled);
    }
}

void CustomMenu::DrawSeparator(wxDC &dc, const wxRect &rect)
{
    wxColour sepColor = UIColors::HeaderDivider();
    int penWidth = std::max(1, static_cast<int>(m_dpiScale));
    dc.SetPen(wxPen(sepColor, penWidth));

    int y = rect.y + rect.height / 2;
    int x1 = rect.x + m_scaledPadding + (m_iconWidth > 0 ? m_iconWidth : m_scaledIndent);
    int x2 = rect.GetRight() - m_scaledPadding;
    dc.DrawLine(x1, y, x2, y);
}

void CustomMenu::DrawSubmenuArrow(wxDC &dc, const wxRect &rect, bool isHovered)
{
    wxColour arrowColor = isHovered ? UIColors::MenuText() : UIColors::SecondaryText();

    int arrowSize = m_scaledArrowSize;
    int x = rect.GetRight() - m_scaledPadding - arrowSize - m_scaledSubmenuGap;
    int y = rect.y + (rect.height - arrowSize) / 2;

    wxPoint points[3] = {wxPoint(x, y), wxPoint(x + arrowSize, y + arrowSize / 2), wxPoint(x, y + arrowSize)};

    dc.SetBrush(wxBrush(arrowColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawPolygon(3, points);
}

void CustomMenu::DrawCheckmark(wxDC &dc, const wxRect &rect, bool /*isHovered*/)
{
    wxColour checkColor = UIColors::AccentPrimary();

    int size = m_scaledCheckSize;
    int offset = static_cast<int>(2 * m_dpiScale);
    int x = rect.x + (rect.width - size) / 2 + offset;
    int y = rect.y + (rect.height - size) / 2;

    int penWidth = std::max(2, static_cast<int>(2 * m_dpiScale));
    dc.SetPen(wxPen(checkColor, penWidth));
    dc.DrawLine(x, y + size / 2, x + size / 3, y + size - offset);
    dc.DrawLine(x + size / 3, y + size - offset, x + size, y);
}

int CustomMenu::HitTest(const wxPoint &pt) const
{
    if (pt.x < 0 || pt.x >= GetSize().x)
        return -1;

    int y = m_scaledPadding;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        int itemH = m_items[i].isSeparator ? m_separatorHeight : m_itemHeight;
        if (pt.y >= y && pt.y < y + itemH)
        {
            if (m_items[i].isSeparator)
                return -1;
            return static_cast<int>(i);
        }
        y += itemH;
    }
    return -1;
}

wxRect CustomMenu::GetItemRect(int index) const
{
    if (index < 0 || index >= (int) m_items.size())
        return wxRect();

    int y = m_scaledPadding;
    for (int i = 0; i < index; ++i)
    {
        y += m_items[i].isSeparator ? m_separatorHeight : m_itemHeight;
    }

    int itemH = m_items[index].isSeparator ? m_separatorHeight : m_itemHeight;
    // Use m_totalWidth instead of GetSize().x for more reliable positioning
    int width = m_totalWidth > 0 ? m_totalWidth : GetSize().x;
    return wxRect(0, y, width, itemH);
}

void CustomMenu::OnMouseMove(wxMouseEvent &evt)
{
    int index = HitTest(evt.GetPosition());

    if (index != m_hoverIndex)
    {
        m_hoverIndex = index;
        Refresh();

        // Handle submenu opening with delay using static timer
        StopSubmenuTimer();

        if (index >= 0 && m_items[index].submenu && m_items[index].enabled)
        {
            // If different from current open submenu, start timer to open
            if (index != m_submenuItemIndex)
            {
                m_submenuClickLock = false; // Clear lock when hovering on different submenu item
                m_pendingSubmenuIndex = index;
                StartSubmenuTimer(this, index);
            }
            // Note: When hovering back on the submenu item, do NOT clear the click lock
            // The lock should only be cleared when opening a different submenu
        }
        else if (m_openSubmenu && index != m_submenuItemIndex)
        {
            // Only start close timer if click lock is not active
            // (click lock prevents accidental close right after clicking a submenu item)
            if (!m_submenuClickLock)
            {
                m_pendingSubmenuIndex = -1;
                StartSubmenuTimer(this, -1);
            }
        }
    }
    else if (index >= 0 && m_items[index].submenu && m_items[index].enabled && !m_openSubmenu)
    {
        // Same item but submenu is closed - reopen it
        m_pendingSubmenuIndex = index;
        StartSubmenuTimer(this, index);
    }
}

void CustomMenu::OnMouseDown(wxMouseEvent &evt)
{
    // For submenu items, open immediately on mouse down (don't wait for mouse up)
    int index = HitTest(evt.GetPosition());
    if (index >= 0 && m_items[index].submenu && m_items[index].enabled)
    {
        StopSubmenuTimer();
        m_pendingSubmenuIndex = -1;
        m_hoverIndex = index;
        Refresh();
        OpenSubmenu(index);
    }
    // Regular items are handled on mouse up
}

void CustomMenu::OnMouseUp(wxMouseEvent &evt)
{
    int index = HitTest(evt.GetPosition());
    if (index >= 0)
    {
        // For submenu items, do NOT call ActivateItem - it triggers wxPopupTransientWindow
        // to dismiss the submenu. The submenu is already open from HandleClickInMenuHierarchy.
        if (m_items[index].submenu && m_items[index].enabled)
        {
            StopSubmenuTimer();
            m_pendingSubmenuIndex = -1;
            return; // Don't call ActivateItem for submenu items
        }
        ActivateItem(index);
    }
}

bool CustomMenu::ProcessLeftDown(wxMouseEvent &event)
{
    // Use wxGetMousePosition for reliable screen coordinates
    wxPoint screenPt = wxGetMousePosition();

    // Check if click is inside this menu
    wxRect menuRect = GetScreenRect();
    if (menuRect.Contains(screenPt))
    {
        // Click is inside this menu
        wxPoint localPt = ScreenToClient(screenPt);
        int index = HitTest(localPt);
        if (index >= 0 && m_items[index].submenu && m_items[index].enabled)
        {
            // Clicked on a submenu item - open it immediately on mouse down
            StopSubmenuTimer();
            m_pendingSubmenuIndex = -1;
            m_hoverIndex = index;
            Refresh();
            OpenSubmenu(index);
        }
        // Return false to keep the popup open and allow OnMouseUp to handle regular items
        return false;
    }

    // Check if click is inside an open submenu (recursive)
    if (m_openSubmenu && m_openSubmenu->ContainsPoint(screenPt))
    {
        // Let the submenu handle it
        return false;
    }

    // CRITICAL: If this is a submenu, check if click is on parent menu's submenu item
    // This prevents the submenu from being dismissed when clicking on the parent's "Add Shape" etc.
    if (m_parentMenu)
    {
        wxRect parentRect = m_parentMenu->GetScreenRect();
        if (parentRect.Contains(screenPt))
        {
            // Click is on parent menu - check if it's on the submenu item that opened us
            wxPoint parentLocalPt = m_parentMenu->ScreenToClient(screenPt);
            int parentIndex = m_parentMenu->HitTest(parentLocalPt);
            if (parentIndex == m_parentMenu->m_submenuItemIndex)
            {
                // Click is on the submenu item that opened this submenu - don't dismiss
                return false;
            }
        }
    }

    // Click is outside - let the base class dismiss the popup
    return wxPopupTransientWindow::ProcessLeftDown(event);
}

void CustomMenu::OnMouseLeave(wxMouseEvent & /*evt*/)
{
    // Don't clear hover if we have an open submenu
    if (!m_openSubmenu)
    {
        m_hoverIndex = -1;
        Refresh();
    }
}

void CustomMenu::OnKeyDown(wxKeyEvent &evt)
{
    int keyCode = evt.GetKeyCode();

    switch (keyCode)
    {
    case WXK_UP:
        SelectItem(m_hoverIndex - 1);
        break;

    case WXK_DOWN:
        SelectItem(m_hoverIndex + 1);
        break;

    case WXK_LEFT:
        if (m_parentMenu)
        {
            Dismiss();
        }
        else if (m_openSubmenu)
        {
            CloseSubmenu();
        }
        break;

    case WXK_RIGHT:
        if (m_hoverIndex >= 0 && m_items[m_hoverIndex].submenu)
        {
            OpenSubmenu(m_hoverIndex);
        }
        break;

    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
        if (m_hoverIndex >= 0)
        {
            ActivateItem(m_hoverIndex);
        }
        break;

    case WXK_ESCAPE:
        CloseAllSubmenus();
        Dismiss();
        break;

    default:
        // Check for accelerator key
        if (keyCode >= 'A' && keyCode <= 'Z')
        {
            HandleAccelerator(static_cast<wxChar>(keyCode));
        }
        else if (keyCode >= 'a' && keyCode <= 'z')
        {
            HandleAccelerator(static_cast<wxChar>(wxToupper(keyCode)));
        }
        else
        {
            evt.Skip();
        }
        break;
    }
}

void CustomMenu::OnAppActivate(wxActivateEvent &evt)
{
    evt.Skip(); // Let other handlers process the event
    if (!evt.GetActive() && IsShown())
    {
        // App is being deactivated (losing focus) - close this menu
        Dismiss();
    }
}

void CustomMenu::SelectItem(int index)
{
    // Skip separators
    if (index < 0)
    {
        // Wrap to last non-separator
        for (int i = (int) m_items.size() - 1; i >= 0; --i)
        {
            if (!m_items[i].isSeparator)
            {
                index = i;
                break;
            }
        }
    }
    else if (index >= (int) m_items.size())
    {
        // Wrap to first non-separator
        for (int i = 0; i < (int) m_items.size(); ++i)
        {
            if (!m_items[i].isSeparator)
            {
                index = i;
                break;
            }
        }
    }
    else
    {
        // Skip over separators
        int direction = (index > m_hoverIndex) ? 1 : -1;
        while (index >= 0 && index < (int) m_items.size() && m_items[index].isSeparator)
        {
            index += direction;
        }

        if (index < 0 || index >= (int) m_items.size())
        {
            // Wrapped around
            SelectItem(direction > 0 ? 0 : (int) m_items.size() - 1);
            return;
        }
    }

    if (index >= 0 && index < (int) m_items.size())
    {
        m_hoverIndex = index;
        Refresh();
    }
}

void CustomMenu::ActivateItem(int index)
{
    if (index < 0 || index >= (int) m_items.size())
        return;

    const auto &item = m_items[index];

    if (!item.enabled)
        return;

    if (item.submenu)
    {
        OpenSubmenu(index);
        return;
    }

    // Store selected ID before dismissing
    m_selectedId = item.id;

    // IMPORTANT: Find the root menu BEFORE calling CloseAllSubmenus,
    // because CloseAllSubmenus sets m_parentMenu to nullptr and breaks the chain
    CustomMenu *rootMenu = this;
    while (rootMenu->m_parentMenu)
    {
        rootMenu = rootMenu->m_parentMenu;
    }

    // Close all submenus
    CloseAllSubmenus();

    // Hide the root menu (which will hide everything since submenus are gone)
    rootMenu->Hide();

    // Dismiss the root menu properly (triggers OnDismiss for cleanup)
    rootMenu->Dismiss();

    // Also use static tracking for extra safety
    DismissActiveContextMenu();

    // Execute callback after dismissing
    if (item.callback)
    {
        item.callback();
    }
}

void CustomMenu::HandleAccelerator(wxChar key)
{
    for (int i = 0; i < (int) m_items.size(); ++i)
    {
        if (m_items[i].accelerator == key && m_items[i].enabled && !m_items[i].isSeparator)
        {
            m_hoverIndex = i;
            Refresh();
            ActivateItem(i);
            return;
        }
    }
}

void CustomMenu::OpenSubmenu(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= (int) m_items.size())
    {
        return;
    }

    const auto &item = m_items[itemIndex];
    if (!item.submenu)
    {
        return;
    }

    // If same submenu already open, do nothing
    if (m_submenuItemIndex == itemIndex && m_openSubmenu)
    {
        return;
    }

    // Verify this window is valid before proceeding
    if (!GetHandle())
        return;

    // Close existing submenu
    CloseSubmenu();

    // Set up submenu relationships first
    item.submenu->m_parentMenu = this;
    item.submenu->m_eventHandler = m_eventHandler;

    // Create the submenu window BEFORE calculating size (required for correct sizing)
    if (!item.submenu->GetParent())
        item.submenu->Create(this);

    // Calculate size after window is created
    item.submenu->CalculateSize();

    // Position submenu to the right of this item with a small gap (scaled)
    wxRect itemRect = GetItemRect(itemIndex);
    wxPoint screenPos = ClientToScreen(wxPoint(itemRect.GetRight() + m_scaledSubmenuGap, itemRect.GetTop()));

    // Validate the screen position - if ClientToScreen returned garbage, bail out
    if (screenPos.x < -10000 || screenPos.x > 100000 || screenPos.y < -10000 || screenPos.y > 100000)
    {
        return; // Invalid coordinates, don't try to open submenu
    }

    // Check if submenu would go off right edge - use internal calculated width
    int submenuWidth = item.submenu->m_totalWidth;

    int displayIndex = wxDisplay::GetFromWindow(this);
    if (displayIndex == wxNOT_FOUND)
        displayIndex = 0;

    if (displayIndex >= 0 && displayIndex < (int) wxDisplay::GetCount())
    {
        wxDisplay display(displayIndex);
        wxRect screenRect = display.GetClientArea();

        if (screenPos.x + submenuWidth > screenRect.GetRight())
        {
            // Open to the left instead with a small gap (scaled)
            wxPoint leftPos = ClientToScreen(wxPoint(0, 0));
            if (leftPos.x > -10000 && leftPos.x < 100000)
            {
                screenPos.x = leftPos.x - submenuWidth - m_scaledSubmenuGap;
            }
        }
    }

    item.submenu->ShowAt(screenPos, this);

    m_openSubmenu = item.submenu;
    m_submenuItemIndex = itemIndex;
}

void CustomMenu::CloseSubmenu()
{
    if (m_openSubmenu)
    {
        m_openSubmenu->CloseSubmenu(); // Recursive
        m_openSubmenu->Dismiss();
        m_openSubmenu->m_parentMenu = nullptr;
        m_openSubmenu.reset();
        m_submenuItemIndex = -1;
    }
}

void CustomMenu::CloseAllSubmenus()
{
    CloseSubmenu();

    if (m_parentMenu)
    {
        m_parentMenu->CloseAllSubmenus();
    }
}

// Static callback for the submenu timer
static void HandleSubmenuTimerCallback(CustomMenu *menu, int itemIndex)
{
    if (menu && menu->IsShown())
    {
        menu->HandleTimerAction(itemIndex);
    }
}

void CustomMenu::HandleTimerAction(int itemIndex)
{
    if (itemIndex >= 0)
    {
        OpenSubmenu(itemIndex);
    }
    else
    {
        CloseSubmenu();
    }
}

} // namespace GUI
} // namespace Slic3r
