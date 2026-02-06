///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "CollapsibleSection.hpp"
#include "UIColors.hpp"
#include "../GUI_App.hpp"
#include "../wxExtensions.hpp"
#include "Label.hpp"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/settings.h>

namespace Slic3r
{
namespace GUI
{

// DPI scaling helper for chevron pen width
static int GetScaledChevronPenWidth()
{
    return std::max(1, wxGetApp().em_unit() / 5); // 2px at 100%, min 1px
}

wxDEFINE_EVENT(EVT_COLLAPSIBLE_CHANGED, wxCommandEvent);

wxBEGIN_EVENT_TABLE(CollapsibleSection, wxPanel) EVT_PAINT(CollapsibleSection::OnPaint)
    EVT_SIZE(CollapsibleSection::OnSize) wxEND_EVENT_TABLE()

        CollapsibleSection::CollapsibleSection(wxWindow *parent, const wxString &title, bool initially_expanded,
                                               wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxNO_BORDER)
    , m_header_panel(nullptr)
    , m_title_text(nullptr)
    , m_chevron(nullptr)
    , m_icon(nullptr)
    , m_bullet(nullptr)
    , m_badge(nullptr)
    , m_content(nullptr)
    , m_content_container(nullptr)
    , m_pinned_content(nullptr)
    , m_pinned_container(nullptr)
    , m_main_sizer(nullptr)
    , m_header_sizer(nullptr)
    , m_title(title)
    , m_expanded(initially_expanded)
    , m_header_hovered(false)
    , m_compact(false)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    // Initialize colors based on dark/light mode
    UpdateColors();

    // Set the CollapsibleSection's own background color (used in OnPaint)
    SetBackgroundColour(m_content_bg_color);

    // Create chevron bitmaps (simple triangle shapes)
    // We'll use the scaling system to create proper bitmaps
    UpdateChevron();

    CreateHeader();

    // Create pinned container (always visible, for preset dropdowns etc.)
    m_pinned_container = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
    m_pinned_container->SetBackgroundColour(m_content_bg_color);
    m_pinned_container->Hide(); // Hidden until pinned content is set

    // Create content container (collapsible)
    m_content_container = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
    m_content_container->SetBackgroundColour(m_content_bg_color);

    // Main layout: header -> pinned (always visible) -> content (collapsible)
    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    m_main_sizer->Add(m_header_panel, 0, wxEXPAND);
    m_main_sizer->Add(m_pinned_container, 0, wxEXPAND);
    m_main_sizer->Add(m_content_container, 1, wxEXPAND);
    SetSizer(m_main_sizer);

    // Set initial visibility
    m_content_container->Show(m_expanded);

    // Force refresh to ensure background colors are properly applied at startup
    // (Without this, light mode backgrounds may not appear until theme is toggled)
    if (m_content_container)
        m_content_container->Refresh();
    if (m_pinned_container)
        m_pinned_container->Refresh();
    Refresh();
}

void CollapsibleSection::CreateHeader()
{
    int em = wxGetApp().em_unit();
    int header_height = int((m_compact ? COMPACT_HEADER_HEIGHT : HEADER_HEIGHT) * em / 10);
    int padding = int((m_compact ? COMPACT_PADDING : HEADER_PADDING) * em / 10);

    m_header_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, header_height), wxNO_BORDER);
    m_header_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_header_panel->SetBackgroundColour(m_header_normal_color);
    m_header_panel->SetCursor(wxCursor(wxCURSOR_HAND));

    // Handle painting the header background ourselves to ensure color changes are respected
    m_header_panel->Bind(wxEVT_PAINT,
                         [this](wxPaintEvent &)
                         {
                             wxPaintDC dc(m_header_panel);
                             dc.SetBackground(wxBrush(m_header_panel->GetBackgroundColour()));
                             dc.Clear();
                         });

    // Bind mouse events for interaction
    m_header_panel->Bind(wxEVT_LEFT_UP, &CollapsibleSection::OnHeaderClick, this);
    m_header_panel->Bind(wxEVT_ENTER_WINDOW, &CollapsibleSection::OnHeaderEnter, this);
    // Use smart leave handler that checks if mouse is still within the header area
    m_header_panel->Bind(wxEVT_LEAVE_WINDOW,
                         [this](wxMouseEvent &evt)
                         {
                             wxPoint mouse_screen = wxGetMousePosition();
                             wxRect header_rect = m_header_panel->GetScreenRect();
                             if (!header_rect.Contains(mouse_screen))
                                 OnHeaderLeave(evt);
                         });

    // Helper to bind hover events on header child controls so hover covers the entire header
    auto bindChildHover = [this](wxWindow *child)
    {
        child->SetCursor(wxCursor(wxCURSOR_HAND));
        child->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &evt) { OnHeaderEnter(evt); });
        child->Bind(wxEVT_LEAVE_WINDOW,
                    [this](wxMouseEvent &evt)
                    {
                        wxPoint mouse_screen = wxGetMousePosition();
                        wxRect header_rect = m_header_panel->GetScreenRect();
                        if (!header_rect.Contains(mouse_screen))
                            OnHeaderLeave(evt);
                    });
    };

    m_header_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Chevron indicator
    m_chevron = new wxStaticBitmap(m_header_panel, wxID_ANY, m_expanded ? m_chevron_expanded : m_chevron_collapsed);
    m_chevron->Bind(wxEVT_LEFT_UP, &CollapsibleSection::OnHeaderClick, this);
    bindChildHover(m_chevron);
    m_header_sizer->Add(m_chevron, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, padding);

    // Optional bullet (initially hidden) - shows before icon when SetBulletColor is called
    m_bullet = new wxStaticText(m_header_panel, wxID_ANY, wxString::FromUTF8("●"));
    m_bullet->SetForegroundColour(wxColour(0xEA, 0xA0, 0x32)); // Default to preFlight orange
    m_bullet->Hide();
    m_bullet->Bind(wxEVT_LEFT_UP, &CollapsibleSection::OnHeaderClick, this);
    bindChildHover(m_bullet);
    m_header_sizer->Add(m_bullet, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, padding / 2);

    // Optional icon placeholder (initially hidden)
    m_icon = new wxStaticBitmap(m_header_panel, wxID_ANY, wxNullBitmap);
    m_icon->Hide();
    bindChildHover(m_icon);
    m_header_sizer->Add(m_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, padding / 2);

    // Title text - use ellipsis to allow shrinking
    m_title_text = new wxStaticText(m_header_panel, wxID_ANY, m_title, wxDefaultPosition, wxDefaultSize,
                                    wxST_ELLIPSIZE_END);
    m_title_text->SetMinSize(wxSize(std::max(1, em / 10), -1)); // Allow title to shrink (scaled)
    if (m_compact)
        m_title_text->SetFont(m_title_text->GetFont().Scaled(0.85f));
    else
        m_title_text->SetFont(m_title_text->GetFont().Bold());
    // Set proper text color for dark/light mode
    // Don't use wxSystemSettings::GetColour because Windows Dark Mode API is always on
    bool is_dark = wxGetApp().dark_mode();
    m_title_text->SetForegroundColour(is_dark ? UIColors::PanelForegroundDark() : UIColors::InputForegroundLight());
    m_title_text->Bind(wxEVT_LEFT_UP, &CollapsibleSection::OnHeaderClick, this);
    bindChildHover(m_title_text);
    m_header_sizer->Add(m_title_text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, padding);

    // Badge (initially hidden)
    m_badge = new wxStaticText(m_header_panel, wxID_ANY, wxEmptyString);
    m_badge->SetForegroundColour(is_dark ? UIColors::SecondaryTextDark() : UIColors::SecondaryTextLight());
    m_badge->Hide();
    bindChildHover(m_badge);
    m_header_sizer->Add(m_badge, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, padding);

    m_header_panel->SetSizer(m_header_sizer);
}

void CollapsibleSection::UpdateChevron()
{
    int em = wxGetApp().em_unit();
    int size = int((m_compact ? COMPACT_CHEVRON_SIZE : CHEVRON_SIZE) * em / 10);

    // Create simple chevron bitmaps
    // Expanded: pointing down (v)
    // Collapsed: pointing right (>)

    wxBitmap expanded_bmp(size, size);
    wxBitmap collapsed_bmp(size, size);

    auto bg_color = m_header_normal_color;
    // Don't use wxSystemSettings::GetColour because Windows Dark Mode API is always on
    bool is_dark = wxGetApp().dark_mode();
    auto fg_color = is_dark ? UIColors::PanelForegroundDark() : UIColors::InputForegroundLight();

    // Draw expanded chevron (down arrow)
    {
        wxMemoryDC dc(expanded_bmp);
        dc.SetBackground(wxBrush(bg_color));
        dc.Clear();

        wxPoint points[3] = {wxPoint(size / 4, size / 3), wxPoint(size * 3 / 4, size / 3),
                             wxPoint(size / 2, size * 2 / 3)};
        dc.SetPen(wxPen(fg_color, GetScaledChevronPenWidth()));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawLines(3, points);
    }

    // Draw collapsed chevron (right arrow)
    {
        wxMemoryDC dc(collapsed_bmp);
        dc.SetBackground(wxBrush(bg_color));
        dc.Clear();

        wxPoint points[3] = {wxPoint(size / 3, size / 4), wxPoint(size / 3, size * 3 / 4),
                             wxPoint(size * 2 / 3, size / 2)};
        dc.SetPen(wxPen(fg_color, GetScaledChevronPenWidth()));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawLines(3, points);
    }

    m_chevron_expanded = wxBitmapBundle::FromBitmap(expanded_bmp);
    m_chevron_collapsed = wxBitmapBundle::FromBitmap(collapsed_bmp);

    if (m_chevron)
    {
        m_chevron->SetBitmap(m_expanded ? m_chevron_expanded : m_chevron_collapsed);
    }
}

void CollapsibleSection::SetContent(wxWindow *content)
{
    if (m_content)
    {
        m_content->Destroy();
    }

    m_content = content;

    if (m_content)
    {
        m_content->Reparent(m_content_container);

        auto *content_sizer = new wxBoxSizer(wxVERTICAL);
        content_sizer->Add(m_content, 1, wxEXPAND | wxALL, 0);
        m_content_container->SetSizer(content_sizer);
    }

    UpdateLayout();
}

void CollapsibleSection::SetPinnedContent(wxWindow *content)
{
    if (m_pinned_content)
    {
        m_pinned_content->Destroy();
    }

    m_pinned_content = content;

    if (m_pinned_content)
    {
        m_pinned_content->Reparent(m_pinned_container);

        auto *pinned_sizer = new wxBoxSizer(wxVERTICAL);
        pinned_sizer->Add(m_pinned_content, 0, wxEXPAND | wxALL, 0);
        m_pinned_container->SetSizer(pinned_sizer);
        m_pinned_container->Show();
    }
    else
    {
        m_pinned_container->Hide();
    }

    UpdateLayout();
}

void CollapsibleSection::SetExpanded(bool expanded, bool animate)
{
    if (m_expanded == expanded)
        return;

    m_expanded = expanded;

    // Update chevron
    if (m_chevron)
    {
        m_chevron->SetBitmap(m_expanded ? m_chevron_expanded : m_chevron_collapsed);
    }

    // Show/hide content
    m_content_container->Show(m_expanded);

    // Notify listeners
    if (m_on_expand_changed)
    {
        m_on_expand_changed(m_expanded);
    }

    // Send event
    wxCommandEvent evt(EVT_COLLAPSIBLE_CHANGED, GetId());
    evt.SetInt(m_expanded ? 1 : 0);
    evt.SetEventObject(this);
    ProcessWindowEvent(evt);

    UpdateLayout();
}

void CollapsibleSection::ToggleExpanded()
{
    SetExpanded(!m_expanded);
}

void CollapsibleSection::SetTitle(const wxString &title)
{
    m_title = title;
    if (m_title_text)
    {
        m_title_text->SetLabel(title);
    }
}

void CollapsibleSection::SetHeaderIcon(const wxBitmapBundle &icon)
{
    if (m_icon)
    {
        m_icon_bundle = icon; // Store for DPI re-scaling
        if (m_compact && icon.IsOk())
        {
            // For compact mode, render the icon at a smaller size (12px logical, DPI-scaled)
            int em = wxGetApp().em_unit();
            int icon_size = int(COMPACT_CHEVRON_SIZE * em / 10);
            m_icon->SetBitmap(icon.GetBitmap(wxSize(icon_size, icon_size)));
        }
        else
        {
            m_icon->SetBitmap(icon);
        }
        bool has_icon = icon.IsOk();
        m_icon->Show(has_icon);
        // Hide chevron when icon is shown (icon replaces chevron as expand indicator)
        if (m_chevron)
        {
            m_chevron->Show(!has_icon);
        }
        m_header_panel->Layout();
    }
}

void CollapsibleSection::SetHeaderIndent(int indent)
{
    if (m_header_sizer && indent > 0)
    {
        // Insert spacer at the beginning of the header sizer
        m_header_sizer->Insert(0, indent, 0, 0);
        m_header_panel->Layout();
    }
}

void CollapsibleSection::SetBadgeText(const wxString &text)
{
    if (m_badge)
    {
        m_badge->SetLabel(text);
        m_badge->Show(!text.IsEmpty());
        m_header_panel->Layout();
    }
}

void CollapsibleSection::SetBadgeVisible(bool visible)
{
    if (m_badge)
    {
        m_badge->Show(visible);
        m_header_panel->Layout();
    }
}

void CollapsibleSection::SetBulletColor(const wxColour &color)
{
    if (m_bullet)
    {
        m_bullet->SetForegroundColour(color);
        m_bullet->Show(true);
        m_bullet->Refresh();
        m_header_panel->Layout();
    }
}

void CollapsibleSection::SetCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;

    int em = wxGetApp().em_unit();
    int header_height = int((m_compact ? COMPACT_HEADER_HEIGHT : HEADER_HEIGHT) * em / 10);

    // Update header height
    if (m_header_panel)
        m_header_panel->SetMinSize(wxSize(-1, header_height));

    // Update title font - compact uses smaller non-bold, normal uses bold
    if (m_title_text)
    {
        wxFont font = m_title_text->GetFont();
        // Reset to base font first
        font.SetWeight(wxFONTWEIGHT_NORMAL);
        if (m_compact)
            font = font.Scaled(0.85f);
        else
            font.SetWeight(wxFONTWEIGHT_BOLD);
        m_title_text->SetFont(font);
    }

    // Update bullet font for compact mode
    if (m_bullet && m_compact)
    {
        wxFont bullet_font = m_bullet->GetFont().Scaled(0.85f);
        m_bullet->SetFont(bullet_font);
    }

    // Regenerate chevron at new size
    UpdateChevron();
    UpdateLayout();
}

void CollapsibleSection::SetHeaderBackgroundColor(const StateColor &color)
{
    m_header_bg_color = color;
    // Extract direct colors for reliable theme switching
    m_header_normal_color = m_header_bg_color.colorForStates(StateColor::Normal);
    m_header_hover_color = m_header_bg_color.colorForStates(StateColor::Hovered);
    if (!m_header_hover_color.IsOk())
        m_header_hover_color = m_header_normal_color;

    if (m_header_panel)
    {
        m_header_panel->SetBackgroundColour(m_header_hovered ? m_header_hover_color : m_header_normal_color);
    }
    UpdateChevron();
}

void CollapsibleSection::SetContentBackgroundColor(const wxColour &color)
{
    m_content_bg_color = color;
    if (m_content_container)
    {
        m_content_container->SetBackgroundColour(color);
    }
}

void CollapsibleSection::OnHeaderClick(wxMouseEvent &evt)
{
    ToggleExpanded();
    evt.Skip();
}

void CollapsibleSection::OnHeaderEnter(wxMouseEvent &evt)
{
    m_header_hovered = true;
    if (m_header_panel)
    {
        m_header_panel->SetBackgroundColour(m_header_hover_color);
        m_header_panel->Refresh();
    }
    evt.Skip();
}

void CollapsibleSection::OnHeaderLeave(wxMouseEvent &evt)
{
    m_header_hovered = false;
    if (m_header_panel)
    {
        m_header_panel->SetBackgroundColour(m_header_normal_color);
        m_header_panel->Refresh();
    }
    evt.Skip();
}

void CollapsibleSection::OnPaint(wxPaintEvent &evt)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
}

void CollapsibleSection::OnSize(wxSizeEvent &evt)
{
    evt.Skip();
    Refresh();
}

void CollapsibleSection::UpdateLayout()
{
    if (GetSizer())
    {
        GetSizer()->Layout();
    }

    // Propagate layout change up the hierarchy
    wxWindow *parent = GetParent();
    while (parent)
    {
        parent->Layout();
        if (auto *sizer = parent->GetSizer())
        {
            sizer->Layout();
        }
        parent = parent->GetParent();
    }

    Refresh();
}

void CollapsibleSection::msw_rescale()
{
    int em = wxGetApp().em_unit();
    int header_height = int((m_compact ? COMPACT_HEADER_HEIGHT : HEADER_HEIGHT) * em / 10);

    if (m_header_panel)
    {
        m_header_panel->SetMinSize(wxSize(-1, header_height));
    }

    // Re-apply compact icon at new DPI scale
    if (m_compact && m_icon && m_icon_bundle.IsOk())
    {
        int icon_size = int(COMPACT_CHEVRON_SIZE * em / 10);
        m_icon->SetBitmap(m_icon_bundle.GetBitmap(wxSize(icon_size, icon_size)));
    }

    UpdateChevron();
    UpdateLayout();
}

void CollapsibleSection::UpdateColors()
{
    bool is_dark = wxGetApp().dark_mode();

    if (is_dark)
    {
        // Dark mode colors - preFlight warm tint
        m_header_normal_color = UIColors::HeaderBackgroundDark();
        m_header_hover_color = UIColors::HeaderHoverDark();
        m_content_bg_color = UIColors::ContentBackgroundDark();
    }
    else
    {
        // Light mode colors
        m_header_normal_color = UIColors::HeaderBackgroundLight();
        m_header_hover_color = UIColors::HeaderHoverLight();
        m_content_bg_color = UIColors::ContentBackgroundLight();
    }

    // Update StateColor for compatibility with existing code
    m_header_bg_color = StateColor();
    m_header_bg_color.append(m_header_normal_color, StateColor::Normal);
    m_header_bg_color.append(m_header_hover_color, StateColor::Hovered);
}

void CollapsibleSection::sys_color_changed()
{
    UpdateColors();

    // Don't use wxSystemSettings::GetColour because Windows Dark Mode API is always on
    bool is_dark = wxGetApp().dark_mode();
    wxColour text_color = is_dark ? UIColors::PanelForegroundDark() : UIColors::InputForegroundLight();

    // Set the CollapsibleSection's own background color (used in OnPaint)
    SetBackgroundColour(m_content_bg_color);

    if (m_header_panel)
    {
        // Use direct color variables instead of StateColor for reliable updates
        wxColour header_color = m_header_hovered ? m_header_hover_color : m_header_normal_color;
        m_header_panel->SetBackgroundColour(header_color);
    }
    if (m_title_text)
    {
        m_title_text->SetForegroundColour(text_color);
    }
    if (m_bullet)
    {
        // Bullet keeps its color (usually brand orange) but needs refresh
        m_bullet->Refresh();
    }
    if (m_badge)
    {
        m_badge->SetForegroundColour(is_dark ? UIColors::SecondaryTextDark() : UIColors::SecondaryTextLight());
    }
    if (m_content_container)
    {
        m_content_container->SetBackgroundColour(m_content_bg_color);
    }
    if (m_pinned_container)
    {
        m_pinned_container->SetBackgroundColour(m_content_bg_color);
    }

    // Update chevron for new theme colors
    UpdateChevron();

    // Force refresh and update of all elements on Windows
    if (m_header_panel)
    {
        m_header_panel->Refresh();
        m_header_panel->Update();
    }
    Refresh();
    Update();
}

} // namespace GUI
} // namespace Slic3r
