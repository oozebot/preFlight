///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2021 David Kocík @kocikdav, Oleksandra Iushchenko @YuSanka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/wx.h>
#include <wx/html/htmlwin.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

class ScrollablePanel; // themed-scrollbar host for the OpenGL-info HTML (global namespace)

namespace DSKY
{

class SysInfoDialog : public DPIDialog
{
    ScalableBitmap m_logo_bmp;
    wxStaticBitmap *m_logo;
    wxHtmlWindow *m_opengl_info_html;
    ScrollablePanel *m_opengl_info_panel{nullptr};
    wxHtmlWindow *m_html;

    wxButton *m_btn_copy_to_clipboard;

public:
    SysInfoDialog();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void onCopyToClipboard(wxEvent &);
    void onCloseDialog(wxEvent &);
};
} // namespace DSKY

