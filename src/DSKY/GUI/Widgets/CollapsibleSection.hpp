///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "StaticBox.hpp"
#include "StateColor.hpp"
#include "../wxExtensions.hpp"
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/bitmap.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include <functional>

namespace DSKY
{

/**
 * CollapsibleSection - A modern accordion-style collapsible container
 *
 * Features:
 * - Clickable header with expand/collapse chevron
 * - Smooth state transitions
 * - Optional icon and badge on header
 * - Remembers collapsed state
 * - Proper DPI scaling
 */
class CollapsibleSection : public wxPanel
{
public:
    CollapsibleSection(wxWindow *parent, const wxString &title, bool initially_expanded = true,
                       wxWindowID id = wxID_ANY);
    ~CollapsibleSection() override;

    // Content management
    void SetContent(wxWindow *content);
    wxWindow *GetContent() const { return m_content; }
    wxWindow *GetContentContainer() const { return m_content_container; }

    // Pinned content - always visible even when collapsed (e.g., preset dropdowns)
    void SetPinnedContent(wxWindow *content);
    wxWindow *GetPinnedContent() const { return m_pinned_content; }

    // Expand/collapse control. relayout=false skips the ancestor re-layout so a batch of changes can
    // be laid out once by the caller.
    void SetExpanded(bool expanded, bool animate = false, bool relayout = true);
    bool IsExpanded() const { return m_expanded; }
    void ToggleExpanded();

    // Header customization
    void SetTitle(const wxString &title);
    wxString GetTitle() const { return m_title; }
    void SetHeaderIcon(const wxBitmapBundle &icon);
    void SetHeaderIndent(int indent); // Indent header content from left edge
    void SetTitleAccent(bool enable); // Render the title text in the themed accent color (sidebar section headers)
    void SetBadgeText(const wxString &text);
    void SetBadgeVisible(bool visible);
    void SetBulletColor(const wxColour &color); // Show colored bullet before icon
    void SetCompact(bool compact);              // Use smaller header, icons, and font for sub-tabs
    // When false: always expanded, blank chevron slot, no click-to-collapse. relayout as for SetExpanded.
    void SetCollapsible(bool collapsible, bool relayout = true);
    bool IsCollapsible() const { return m_collapsible; }
    // Whether a header click re-lays out every ancestor (default). Off when the owner lays out the
    // scroll area itself from the expand callback.
    void SetRelayoutOnToggle(bool relayout) { m_relayout_on_toggle = relayout; }
    void SetHeaderVisible(bool visible); // Show/hide the entire header row

    // Styling
    void SetHeaderBackgroundColor(const StateColor &color);
    void SetContentBackgroundColor(const wxColour &color);

    // Events - called when expand state changes
    void SetOnExpandChanged(std::function<void(bool)> callback) { m_on_expand_changed = callback; }

    // For DPI scaling
    void msw_rescale();
    void sys_color_changed();

private:
    void UpdateColors();

protected:
    void OnHeaderClick(wxMouseEvent &evt);
    void OnHeaderEnter(wxMouseEvent &evt);
    void OnHeaderLeave(wxMouseEvent &evt);
    void OnPaint(wxPaintEvent &evt);
    void OnSize(wxSizeEvent &evt);

    void UpdateLayout();
    void UpdateChevron();
    void ApplyChevronBitmap(); // state chevron, or the blank of the same size when not collapsible
    void CreateHeader();

private:
    // Header components
    wxPanel *m_header_panel;
    wxStaticText *m_title_text;
    wxStaticBitmap *m_chevron;
    wxStaticBitmap *m_icon;
    wxStaticText *m_bullet;
    wxStaticText *m_badge;

    // Content
    wxWindow *m_content;
    wxPanel *m_content_container;

    // Pinned content (always visible, between header and collapsible content)
    wxWindow *m_pinned_content;
    wxPanel *m_pinned_container;

    // Layout
    wxBoxSizer *m_main_sizer;
    wxBoxSizer *m_header_sizer;

    // State
    wxString m_title;
    bool m_expanded;
    bool m_header_hovered;
    bool m_compact;
    bool m_collapsible{true};
    bool m_relayout_on_toggle{true};
    bool m_title_accent{false}; // When true, the title text uses the themed accent color

    // Styling
    StateColor m_header_bg_color;
    wxColour m_header_normal_color;
    wxColour m_header_hover_color;
    wxColour m_content_bg_color;

    // Bitmaps
    wxBitmapBundle m_chevron_expanded;
    wxBitmapBundle m_chevron_collapsed;
    wxBitmapBundle m_chevron_blank; // keeps the chevron slot when the header is not collapsible
    wxBitmapBundle m_icon_bundle;   // Stored for compact mode DPI re-scaling

    // Callback
    std::function<void(bool)> m_on_expand_changed;

    // Constants
    static constexpr int HEADER_HEIGHT = 32;
    static constexpr int HEADER_PADDING = 8;
    static constexpr int CHEVRON_SIZE = 16;
    // Compact mode (sub-tabs) - 75% of normal
    static constexpr int COMPACT_HEADER_HEIGHT = 24;
    static constexpr int COMPACT_PADDING = 6;
    static constexpr int COMPACT_CHEVRON_SIZE = 23;

    wxDECLARE_EVENT_TABLE();
};

// Event for expand/collapse state change
wxDECLARE_EVENT(EVT_COLLAPSIBLE_CHANGED, wxCommandEvent);

} // namespace DSKY

