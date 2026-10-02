///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ScrollablePanel.hpp"
#include "ScrollBar.hpp"
#include "UIColors.hpp"
#include "../GUI_App.hpp"
#include "../GuiBudget.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include <wx/dcclient.h>
#include <algorithm>

// DPI scaling helpers
static int GetScaledScrollbarWidth()
{
    return static_cast<int>(DSKY::wxGetApp().em_unit() * 1.2); // 12px at 100%, matches ScrollBar
}

static int GetScaledScrollAmount()
{
    return DSKY::wxGetApp().em_unit() * 4; // 40px at 100%
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
    if (!child || !m_content)
        return;

    // Walk up the parent chain to compute the child's position relative to m_content.
    // This handles deeply nested children (e.g. field widgets inside OG_CustomCtrl inside Page).
    wxPoint posInContent(0, 0);
    for (wxWindow *w = child; w && w != m_content; w = w->GetParent())
    {
        wxPoint p = w->GetPosition();
        posInContent.x += p.x;
        posInContent.y += p.y;
    }

    wxSize childSize = child->GetSize();
    wxSize mySize = GetClientSize();

    // Get child's position relative to visible area
    int childTop = posInContent.y - m_scrollPosition;
    int childBottom = childTop + childSize.y;

    if (childTop < 0)
    {
        ScrollToPosition(posInContent.y);
    }
    else if (childBottom > mySize.y)
    {
        ScrollToPosition(posInContent.y + childSize.y - mySize.y);
    }
}

int ScrollablePanel::ContentWidthFor(int contentHeight) const
{
    const wxSize mySize = GetClientSize();
    // A gap before the scrollbar for visual centering (~4px at 100%)
    return contentHeight > mySize.y ? mySize.x - GetScaledScrollbarWidth() - GetScaledScrollAmount() / 10 : mySize.x;
}

// The layout below sends the content's children size events, and a child may ask for an update from
// there (the About dialog's HTML measures its new height): a nested update would apply its state and
// return to the outer one, which would then apply its older state. The request is noted instead and
// the running update measures again, a few times at most.
void ScrollablePanel::UpdateScrollbar()
{
    if (m_updating)
    {
        m_updateAgain = true;
        return;
    }
    m_updating = true;
    constexpr int MAX_PASSES = 3;
    for (int pass = 0; pass < MAX_PASSES; ++pass)
    {
        m_updateAgain = false;
        DoUpdateScrollbar();
        if (!m_updateAgain)
            break;
    }
    // Still asked for another after the last pass: the content's layout does not settle
    if (m_updateAgain)
        DBG_COUNT_LOAD("GUI_SCROLL_UPDATE_UNSETTLED");
    m_updating = false;
}

void ScrollablePanel::DoUpdateScrollbar()
{
    if (!m_content || !m_scrollbar)
        return;

    wxSize mySize = GetClientSize();
    if (mySize.x <= 0 || mySize.y <= 0)
        return; // Not laid out yet

    // The content's natural size from its sizer, before any layout: a layout at the current width
    // first would lay every row out twice whenever the scrollbar comes or goes, and the width with it
    wxSizer *contentSizer = m_content->GetSizer();
    wxSize contentSize = contentSizer != nullptr ? contentSizer->GetMinSize() : m_content->GetBestSize();

    // Determine if we need scrollbar
    bool needsScroll = contentSize.y > mySize.y;

    // Calculate available width for content (with gap before scrollbar for visual centering)
    int scrollbarWidth = GetScaledScrollbarWidth();  // Match ScrollBar width
    int scrollbarGap = GetScaledScrollAmount() / 10; // Small gap between content and scrollbar (~4px at 100%)
    int contentWidth = ContentWidthFor(contentSize.y);

    // Store content height
    m_contentHeight = contentSize.y;

    // Content past the window coordinate range is counted, and a tagged panel's height traced
    if (m_contentHeight != m_reportedHeight)
    {
        m_reportedHeight = m_contentHeight;
        DSKY::GuiBudget::content_height(m_budgetTag, m_contentHeight, m_contentOverflowing);
    }

    // Size and position content panel: a new size lays the content out through its size event, an
    // unchanged one sends none, so the content is laid out here
    const wxSize contentTarget(contentWidth, std::max(m_contentHeight, mySize.y));
    const int oldWidth = m_content->GetSize().GetWidth();
    if (m_content->GetSize() != contentTarget)
        m_content->SetSize(contentTarget);
    else
        m_content->Layout();
    m_content->SetPosition(wxPoint(0, -m_scrollPosition));
    // Content that narrows (the scrollbar came) uncovers a strip of this panel. A frozen content
    // (a section toggling) does not invalidate what it uncovers, which would keep the old pixels
    // there, so the strip is repainted here.
    if (contentWidth < oldWidth)
        RefreshRect(wxRect(contentWidth, 0, mySize.x - contentWidth, mySize.y));

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

void ScrollablePanel::SetTrackColour(const wxColour &colour)
{
    if (m_scrollbar)
        m_scrollbar->SetTrackColour(colour);
}

void ScrollablePanel::sys_color_changed()
{
    bool is_dark = DSKY::wxGetApp().dark_mode();

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
    if (m_wheelPassesAtEnds)
    {
        const int maxScroll = std::max(0, m_contentHeight - GetClientSize().y);
        const bool up = event.GetWheelRotation() > 0;
        if ((up && m_scrollPosition <= 0) || (!up && m_scrollPosition >= maxScroll))
        {
            m_sumWheelRotation = 0;
            event.Skip();
            return;
        }
    }

    // Accumulate partial wheel rotations for XWayland compatibility (credit: topisani)
    int delta = event.GetWheelDelta();
    if (delta == 0)
        return;
    m_sumWheelRotation += event.GetWheelRotation() * GetScaledScrollAmount();
    int scrollAmount = m_sumWheelRotation / delta;
    m_sumWheelRotation -= scrollAmount * delta;
    if (scrollAmount == 0)
        return;

    ScrollToPosition(m_scrollPosition - scrollAmount);
}
