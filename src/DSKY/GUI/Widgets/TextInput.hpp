///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/textctrl.h>
#include <wx/brush.h>
#include "StaticBox.hpp"
#include "ThemedTextCtrl.hpp"

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

class ScrollBar;

class TextInput : public wxNavigationEnabled<StaticBox>
{
    wxSize labelSize;
    wxBitmapBundle icon;
    ScalableBitmap drop_down_icon;
    StateColor label_color;
    StateColor text_color;
    DSKY::ThemedTextCtrl *text_ctrl{nullptr};
    ScrollBar *m_scrollbar{nullptr}; // Custom themed scrollbar for multiline
#ifdef _WIN32
    HBRUSH m_hEditBgBrush = NULL; // Native GDI brush for WM_CTLCOLOREDIT response
#endif

    static const int TextInputWidth = 200;
    static const int TextInputHeight = 50;

    wxRect dd_icon_rect;
    wxRect icon_rect;
    std::function<void()> OnClickDropDownIcon{nullptr};
    std::function<void()> OnClickIconFn{nullptr};

public:
    TextInput();
    virtual ~TextInput();

    TextInput(wxWindow *parent, wxString text, wxString label = "", wxString icon = "",
              const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = 0);

public:
    void Create(wxWindow *parent, wxString text, wxString label = "", wxString icon = "",
                const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = 0);

    void SetLabel(const wxString &label) wxOVERRIDE;

    void SetIcon(const wxBitmapBundle &icon);

    void SetLabelColor(StateColor const &color);

    void SetBGColor(StateColor const &color);

    void SetTextColor(StateColor const &color);

    void SetCtrlSize(wxSize const &size);

    virtual void Rescale();

    bool SetFont(const wxFont &font) override;

    virtual bool Enable(bool enable = true) override;

    virtual void SetMinSize(const wxSize &size) override;

    bool SetBackgroundColour(const wxColour &colour) override;

    bool SetForegroundColour(const wxColour &colour) override;

    wxTextCtrl *GetTextCtrl() { return text_ctrl; }

    wxTextCtrl const *GetTextCtrl() const { return text_ctrl; }

    void SetValue(const wxString &value);

    wxString GetValue();

    void SetSelection(long from, long to);

    void SysColorsChanged();

    void SetOnDropDownIcon(std::function<void()> click_drop_down_icon_fn)
    {
        OnClickDropDownIcon = click_drop_down_icon_fn;
    }

    void SetOnClickIcon(std::function<void()> click_icon_fn) { OnClickIconFn = click_icon_fn; }

    wxRect GetIconRect() const { return icon_rect; }

protected:
    bool HasIconClickHandler() const { return OnClickIconFn != nullptr; }

    // Returns true if the click was on the icon and the handler was called
    bool HandleIconClick(const wxPoint &pos)
    {
        if (OnClickIconFn && !icon_rect.IsEmpty() && icon_rect.Contains(pos))
        {
            OnClickIconFn();
            return true;
        }
        return false;
    }

    virtual void OnEdit() {}

#ifdef _WIN32
    // Handle WM_CTLCOLOREDIT from child ThemedTextCtrl
    virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) wxOVERRIDE;
#endif

    void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) wxOVERRIDE;

    void DoSetToolTipText(wxString const &tip) override;

    StateColor GetTextColor() const { return text_color; }
    StateColor GetBorderColor() const { return border_color; }

private:
    void paintEvent(wxPaintEvent &evt);

    void render(wxDC &dc);

    void messureSize();
    void SyncScrollbar(); // Sync the custom scrollbar with text_ctrl scroll state

    DECLARE_EVENT_TABLE()
};
