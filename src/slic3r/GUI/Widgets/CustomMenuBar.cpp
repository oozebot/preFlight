///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "CustomMenuBar.hpp"
#include "UIColors.hpp"
#include "../GUI_App.hpp"

#include <wx/dcbuffer.h>

namespace Slic3r
{
namespace GUI
{

// DPI scaling helper functions - use em_unit system for consistency with ModernTabBar/Sidebar
// At 100% DPI, em_unit() returns 10, so these return the base pixel values
static int GetScaledMenuBarHeight()
{
    return static_cast<int>(2.0 * wxGetApp().em_unit()); // 20px at 100%
}

static int GetScaledItemPadding()
{
    return static_cast<int>(0.8 * wxGetApp().em_unit()); // 8px at 100%
}

static int GetScaledItemVertPadding()
{
    return static_cast<int>(0.4 * wxGetApp().em_unit()); // 4px at 100%
}

static int GetScaledItemSpacing()
{
    return static_cast<int>(0.4 * wxGetApp().em_unit()); // 4px at 100%
}

// ============================================================================
// SwitchingMenusGuard Implementation
// ============================================================================

SwitchingMenusGuard::SwitchingMenusGuard(CustomMenuBar *bar) : m_bar(bar)
{
    if (m_bar)
        m_bar->SetSwitchingMenus(true);
}

SwitchingMenusGuard::~SwitchingMenusGuard()
{
    if (m_bar)
        m_bar->SetSwitchingMenus(false);
}

// ============================================================================
// CustomMenuBarItem Implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(CustomMenuBarItem, wxPanel) EVT_PAINT(CustomMenuBarItem::OnPaint)
    EVT_ENTER_WINDOW(CustomMenuBarItem::OnMouseEnter) EVT_LEAVE_WINDOW(CustomMenuBarItem::OnMouseLeave)
        EVT_LEFT_DOWN(CustomMenuBarItem::OnLeftDown) EVT_LEFT_UP(CustomMenuBarItem::OnLeftUp) wxEND_EVENT_TABLE()

            CustomMenuBarItem::CustomMenuBarItem(wxWindow *parent, const wxString &label,
                                                 std::shared_ptr<CustomMenu> menu)
    : wxPanel(parent, wxID_ANY), m_label(label), m_menu(menu)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Parse label for accelerator key (character after &)
    m_displayLabel = label;
    int ampPos = m_displayLabel.Find('&');
    if (ampPos != wxNOT_FOUND && ampPos + 1 < (int) m_displayLabel.Length())
    {
        m_accelerator = wxToupper(m_displayLabel[ampPos + 1]);
        m_displayLabel.Remove(ampPos, 1); // Remove just the &
    }

    // Calculate size based on text - all dimensions scaled for DPI
    wxClientDC dc(this);
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    wxSize textSize = dc.GetTextExtent(m_displayLabel);

    int padding = GetScaledItemPadding();
    int height = textSize.y + GetScaledItemVertPadding();

    SetMinSize(wxSize(textSize.x + padding * 2, height));
    SetMaxSize(wxSize(-1, height));
}

void CustomMenuBarItem::SetSelected(bool selected)
{
    if (m_selected != selected)
    {
        m_selected = selected;
        Refresh();
    }
}

void CustomMenuBarItem::OnMenuModeEnter()
{
    // Called when menu mode is active and mouse enters this item
    if (auto *menuBar = dynamic_cast<CustomMenuBar *>(GetParent()))
    {
        if (menuBar->IsInMenuMode() && !m_selected)
        {
            ShowMenu();
        }
    }
}

void CustomMenuBarItem::OnPaint(wxPaintEvent & /*evt*/)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetSize();

    // Background color
    wxColour bgColor = UIColors::MenuBackground();
    wxColour hoverBg = UIColors::MenuHover();
    wxColour textColor = UIColors::MenuText();

    // Draw background
    if (m_selected || m_hovered)
    {
        dc.SetBrush(wxBrush(hoverBg));
    }
    else
    {
        dc.SetBrush(wxBrush(bgColor));
    }
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, size.x, size.y);

    // Draw text
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    dc.SetTextForeground(textColor);

    wxSize textSize = dc.GetTextExtent(m_displayLabel);
    int x = (size.x - textSize.x) / 2;
    int y = (size.y - textSize.y) / 2;

    dc.DrawText(m_displayLabel, x, y);
}

void CustomMenuBarItem::OnMouseEnter(wxMouseEvent & /*evt*/)
{
    m_hovered = true;
    Refresh();

    // If menu mode is active, show this menu
    if (auto *menuBar = dynamic_cast<CustomMenuBar *>(GetParent()))
    {
        if (menuBar->IsInMenuMode())
        {
            ShowMenu();
        }
    }
}

void CustomMenuBarItem::OnMouseLeave(wxMouseEvent & /*evt*/)
{
    m_hovered = false;
    if (!m_selected)
    {
        Refresh();
    }
}

void CustomMenuBarItem::OnLeftDown(wxMouseEvent & /*evt*/)
{
    m_pressed = true;

    if (auto *menuBar = dynamic_cast<CustomMenuBar *>(GetParent()))
    {
        if (menuBar->IsInMenuMode() && m_selected)
        {
            // Click on already-open menu - close it
            HideMenu();
            menuBar->ExitMenuMode();
        }
        else
        {
            // Enter menu mode and show this menu
            menuBar->EnterMenuMode(this);
            ShowMenu();
        }
    }
}

void CustomMenuBarItem::OnLeftUp(wxMouseEvent & /*evt*/)
{
    m_pressed = false;
}

void CustomMenuBarItem::ShowMenu()
{
    if (!m_menu)
        return;

    auto *menuBar = dynamic_cast<CustomMenuBar *>(GetParent());

    // Close any other open menus and deselect their items
    // Use RAII guard to ensure m_switchingMenus is always reset even on early return
    {
        SwitchingMenusGuard guard(menuBar);

        if (menuBar)
        {
            for (size_t i = 0; i < menuBar->GetMenuCount(); ++i)
            {
                auto *item = dynamic_cast<CustomMenuBarItem *>(menuBar->GetChildren()[i]);
                if (item && item != this)
                {
                    // Dismiss the menu if it's open
                    auto menu = item->GetMenu();
                    if (menu && menu->IsShown())
                    {
                        menu->Dismiss();
                    }
                    item->SetSelected(false);
                }
            }
        }
    } // guard destructor resets m_switchingMenus

    m_selected = true;
    Refresh();

    // Position menu below this item
    wxPoint pos = ClientToScreen(wxPoint(0, GetSize().y));

    // Create the menu if needed
    if (!m_menu->GetParent())
    {
        m_menu->Create(this);
    }

    // Set event handler
    if (menuBar)
    {
        m_menu->m_eventHandler = menuBar->GetEventHandler();
    }

    m_menu->ShowAt(pos, this);

    // Register as active context menu so mouse filter can dismiss on outside click
    // Menu bar menus don't use m_selfRef, so pass the shared_ptr explicitly
    m_menu->SetAsActiveContextMenu(m_menu);
}

void CustomMenuBarItem::HideMenu()
{
    if (m_menu && m_menu->IsShown())
    {
        m_menu->Dismiss();
    }
    m_selected = false;
    Refresh();
}

// ============================================================================
// CustomMenuBar Implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(CustomMenuBar, wxPanel) EVT_PAINT(CustomMenuBar::OnPaint) EVT_KEY_DOWN(CustomMenuBar::OnKeyDown)
    wxEND_EVENT_TABLE()

        CustomMenuBar::CustomMenuBar(wxWindow *parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(m_sizer);

    // Set height to match native menu bar (scaled for DPI)
    SetMinSize(wxSize(-1, GetScaledMenuBarHeight()));

    // Close menus when the top-level window loses focus
    wxWindow *topLevel = wxGetTopLevelParent(this);
    if (topLevel)
    {
        topLevel->Bind(wxEVT_ACTIVATE,
                       [this](wxActivateEvent &evt)
                       {
                           evt.Skip(); // Let the event propagate
                           if (!evt.GetActive())
                           {
                               // Window is being deactivated - close all menus
                               for (auto *item : m_items)
                               {
                                   auto menu = item->GetMenu();
                                   if (menu && menu->IsShown())
                                   {
                                       menu->Dismiss();
                                   }
                                   item->SetSelected(false);
                               }
                               ExitMenuMode();
                           }
                       });
    }
}

void CustomMenuBar::Append(wxMenu *menu, const wxString &label)
{
    auto customMenu = CustomMenu::FromWxMenu(menu, m_eventHandler);
    Append(customMenu, label);
}

void CustomMenuBar::Append(std::shared_ptr<CustomMenu> menu, const wxString &label)
{
    auto *item = new CustomMenuBarItem(this, label, menu);
    m_items.push_back(item);
    // Add small spacing between items (scaled for DPI, except first item)
    int flags = wxALIGN_CENTER_VERTICAL;
    int border = m_items.size() > 1 ? GetScaledItemSpacing() : 0;
    m_sizer->Add(item, 0, flags | wxLEFT, border);
    Layout();
}

std::shared_ptr<CustomMenu> CustomMenuBar::GetMenu(size_t index) const
{
    if (index < m_items.size())
    {
        return m_items[index]->GetMenu();
    }
    return nullptr;
}

void CustomMenuBar::EnableTop(size_t pos, bool enable)
{
    if (pos < m_items.size())
    {
        m_items[pos]->Enable(enable);
    }
}

void CustomMenuBar::UpdateColors()
{
    Refresh();
    for (auto *item : m_items)
    {
        item->Refresh();
    }
}

void CustomMenuBar::msw_rescale()
{
    // Update menu bar height
    SetMinSize(wxSize(-1, GetScaledMenuBarHeight()));

    // Update each menu item's size
    for (auto *item : m_items)
    {
        // Recalculate size based on text with new DPI scaling
        wxClientDC dc(item);
        dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        wxString label = item->GetMenuLabel();
        // Remove & from label for measurement
        label.Replace("&", "");
        wxSize textSize = dc.GetTextExtent(label);

        int padding = GetScaledItemPadding();
        int height = textSize.y + GetScaledItemVertPadding();

        item->SetMinSize(wxSize(textSize.x + padding * 2, height));
        item->SetMaxSize(wxSize(-1, height));
    }

    Layout();
    Refresh();
}

bool CustomMenuBar::HandleAccelerator(wxChar key)
{
    key = wxToupper(key);
    for (auto *item : m_items)
    {
        if (item->GetAccelerator() == key && item->IsEnabled())
        {
            EnterMenuMode(item);
            item->OnMenuModeEnter();
            return true;
        }
    }
    return false;
}

void CustomMenuBar::EnterMenuMode(CustomMenuBarItem *triggerItem)
{
    m_inMenuMode = true;
    m_activeItem = triggerItem;
}

void CustomMenuBar::ExitMenuMode()
{
    m_inMenuMode = false;
    m_activeItem = nullptr;

    // Deselect all items
    for (auto *item : m_items)
    {
        item->SetSelected(false);
    }
}

void CustomMenuBar::OnMenuDismissed()
{
    // Don't do anything if we're in the middle of switching menus
    if (m_switchingMenus)
        return;

    // Called when any menu is dismissed
    // Only exit menu mode if no menu is currently shown
    bool anyMenuShown = false;
    for (auto *item : m_items)
    {
        if (item->GetMenu() && item->GetMenu()->IsShown())
        {
            anyMenuShown = true;
            break;
        }
    }

    if (!anyMenuShown)
    {
        ExitMenuMode();
    }
}

void CustomMenuBar::OnPaint(wxPaintEvent & /*evt*/)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetSize();

    // Background - solid color, no bottom border (matches native menu bar)
    wxColour bgColor = UIColors::MenuBackground();
    dc.SetBrush(wxBrush(bgColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, size.x, size.y);
}

void CustomMenuBar::OnKeyDown(wxKeyEvent &evt)
{
    if (evt.AltDown())
    {
        int keyCode = evt.GetKeyCode();
        if (keyCode >= 'A' && keyCode <= 'Z')
        {
            if (HandleAccelerator(static_cast<wxChar>(keyCode)))
            {
                return;
            }
        }
        else if (keyCode >= 'a' && keyCode <= 'z')
        {
            if (HandleAccelerator(static_cast<wxChar>(wxToupper(keyCode))))
            {
                return;
            }
        }
    }

    evt.Skip();
}

} // namespace GUI
} // namespace Slic3r
