///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2020 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Pavel Karoukin @hippich
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

class ScrollablePanel; // themed-scrollbar host for the About HTML (global namespace)

namespace DSKY
{

class AboutDialogLogo : public wxPanel
{
public:
    AboutDialogLogo(wxWindow *parent);

private:
    wxBitmap logo;
    void onRepaint(wxEvent &event);
};

class CopyrightsDialog : public DPIDialog
{
public:
    CopyrightsDialog();
    ~CopyrightsDialog() {}

    struct Entry
    {
        Entry(const std::string &lib_name, const std::string &copyright, const std::string &link)
            : lib_name(lib_name), copyright(copyright), link(link)
        {
        }

        std::string lib_name;
        std::string copyright;
        std::string link;
    };

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    wxHtmlWindow *m_html;
    std::vector<Entry> m_entries;

    void onLinkClicked(wxHtmlLinkEvent &event);
    void onCloseDialog(wxEvent &);

    void fill_entries();
    wxString get_html_text();
};

class AboutDialog : public DPIDialog
{
    ScalableBitmap m_logo_bitmap;
    wxHtmlWindow *m_html;
    ScrollablePanel *m_html_panel{nullptr};
    wxStaticBitmap *m_logo;
    int m_copy_rights_btn_id{wxID_ANY};
    int m_copy_version_btn_id{wxID_ANY};

public:
    AboutDialog();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void onLinkClicked(wxHtmlLinkEvent &event);
    void onCloseDialog(wxEvent &);
    void onCopyrightBtn(wxEvent &);
    void onCopyToClipboard(wxEvent &);
};

} // namespace DSKY

