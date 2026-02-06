///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ScrollablePanel.hpp"
#include "ScrollBar.hpp"
#include "UIColors.hpp"
#include "../GUI_App.hpp"
#include <wx/dcclient.h>
#include <algorithm>

// DPI scaling helpers
static int GetScaledScrollbarWidth()
{
    return static_cast<int>(Slic3r::GUI::wxGetApp().em_unit() * 1.2); // 12px at 100%, matches ScrollBar
}

static int GetScaledScrollAmount()
{
    return Slic3r::GUI::wxGetApp().em_unit() * 4; // 40px at 100%
}

wxBEGIN_EVENT_TABLE(ScrollablePanel, wxPanel) EVT_SIZE(ScrollablePanel::OnSize)
    EVT_MOUSEWHEEL(ScrollablePanel::OnMouseWheel) wxEND_EVENT_TABLE()

        ScrollablePanel::ScrollablePanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size,
                                         long style)
    : wxPanel(parent, id, pos, size, style), m_scrollPosition(0), m_contentHeight(0)
{
    // Create content panel directly as child - we'll clip manually via repositioning
    m_content = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_content->Bind(wxEVT_MOUSEWHEEL, &ScrollablePanel::OnMouseWheel, this);

    // Create custom scrollbar
    m_scrollbar = new ScrollBar(this, wxID_ANY);
    m_scrollbar->Bind(wxEVT_SCROLL_THUMBTRACK, &ScrollablePanel::OnScroll, this);
    m_scrollbar->Bind(wxEVT_SCROLL_THUMBRELEASE, &ScrollablePanel::OnScroll, this);
    m_scrollbar->Hide(); // Start hidden until we know we need scrolling

    // Apply initial theme colors
    sys_color_changed();
}

void ScrollablePanel::SetContentSizer(wxSizer *sizer)
{
    m_content->SetSizer(sizer);
    CallAfter([this]() { UpdateScrollbar(); });
}

void ScrollablePanel::ScrollToPosition(int position)
{
    wxSize mySize = GetClientSize();
    int scrollbarWidth = m_scrollbar->IsShown() ? m_scrollbar->GetSize().x : 0;
    int visibleHeight = mySize.y;

    int maxScroll = std::max(0, m_contentHeight - visibleHeight);
    m_scrollPosition = std::max(0, std::min(position, maxScroll));

    // Move content panel up by scroll amount
    m_content->SetPosition(wxPoint(0, -m_scrollPosition));
    m_scrollbar->SetThumbPosition(m_scrollPosition);
}

void ScrollablePanel::ScrollToChild(wxWindow *child)
{
    if (!child || child->GetParent() != m_content)
        return;

    wxPoint childPos = child->GetPosition();
    wxSize childSize = child->GetSize();
    wxSize mySize = GetClientSize();

    // Get child's position relative to visible area
    int childTop = childPos.y - m_scrollPosition;
    int childBottom = childTop + childSize.y;

    if (childTop < 0)
    {
        ScrollToPosition(childPos.y);
    }
    else if (childBottom > mySize.y)
    {
        ScrollToPosition(childPos.y + childSize.y - mySize.y);
    }
}

void ScrollablePanel::UpdateScrollbar()
{
    if (!m_content || !m_scrollbar)
        return;

    wxSize mySize = GetClientSize();
    if (mySize.x <= 0 || mySize.y <= 0)
        return; // Not laid out yet

    // Layout content to get its natural size
    m_content->Layout();
    wxSize contentSize = m_content->GetBestSize();

    // Determine if we need scrollbar
    bool needsScroll = contentSize.y > mySize.y;

    // Calculate available width for content (with gap before scrollbar for visual centering)
    int scrollbarWidth = GetScaledScrollbarWidth();  // Match ScrollBar width
    int scrollbarGap = GetScaledScrollAmount() / 10; // Small gap between content and scrollbar (~4px at 100%)
    int contentWidth = needsScroll ? (mySize.x - scrollbarWidth - scrollbarGap) : mySize.x;

    // Store content height
    m_contentHeight = contentSize.y;

    // Size and position content panel
    m_content->SetSize(contentWidth, std::max(m_contentHeight, mySize.y));
    m_content->SetPosition(wxPoint(0, -m_scrollPosition));

    // Size and position scrollbar (offset by gap for visual centering)
    if (needsScroll)
    {
        m_scrollbar->SetSize(contentWidth + scrollbarGap, 0, scrollbarWidth, mySize.y);
        m_scrollbar->SetScrollbar(m_scrollPosition, mySize.y, m_contentHeight, mySize.y);
        m_scrollbar->Show();
    }
    else
    {
        m_scrollbar->Hide();
        m_scrollPosition = 0;
        m_content->SetPosition(wxPoint(0, 0));
    }

    // Validate scroll position
    if (needsScroll)
    {
        int maxScroll = std::max(0, m_contentHeight - mySize.y);
        if (m_scrollPosition > maxScroll)
            ScrollToPosition(maxScroll);
    }
}

void ScrollablePanel::sys_color_changed()
{
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();

    // Use InputBackground to match ScrollBar's background color
    wxColour bgColor = is_dark ? UIColors::InputBackgroundDark() : UIColors::InputBackgroundLight();

    SetBackgroundColour(bgColor);
    m_content->SetBackgroundColour(bgColor);
    m_scrollbar->sys_color_changed();

    Refresh();
}

int ScrollablePanel::GetScrollPosition() const
{
    return m_scrollPosition;
}

void ScrollablePanel::msw_rescale()
{
    // Update scrollbar with new DPI values
    if (m_scrollbar)
        m_scrollbar->msw_rescale();

    // Recalculate scroll layout
    UpdateScrollbar();
    Refresh();
}

void ScrollablePanel::OnSize(wxSizeEvent &event)
{
    UpdateScrollbar();
    event.Skip();
}

void ScrollablePanel::OnScroll(wxScrollEvent &event)
{
    ScrollToPosition(event.GetPosition());
}

void ScrollablePanel::OnMouseWheel(wxMouseEvent &event)
{
    if (m_contentHeight <= GetClientSize().y)
    {
        event.Skip();
        return;
    }

    int rotation = event.GetWheelRotation();
    int delta = event.GetWheelDelta();

    // Scroll per wheel notch (scaled for DPI)
    int scrollAmount = (rotation / delta) * GetScaledScrollAmount();
    ScrollToPosition(m_scrollPosition - scrollAmount);
}
