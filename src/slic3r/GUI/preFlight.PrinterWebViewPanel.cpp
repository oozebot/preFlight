///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "preFlight.PrinterWebViewPanel.hpp"
#include "WebView.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"

#include <wx/webview.h>
#include <wx/sizer.h>
#include <wx/uri.h>

#include <boost/log/trivial.hpp>

namespace Slic3r
{
namespace GUI
{

PrinterWebViewPanel::PrinterWebViewPanel(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    m_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_sizer);

    // Create the webview immediately
    CreateWebView();
}

PrinterWebViewPanel::~PrinterWebViewPanel()
{
    // wxWebView will be destroyed automatically as a child of this panel
}

void PrinterWebViewPanel::CreateWebView()
{
    if (m_webview_created)
        return;

    m_webview = WebView::webview_new();
    if (m_webview)
    {
        WebView::webview_create(m_webview, this, wxString(""), std::vector<std::string>{});
        m_sizer->Add(m_webview, 1, wxEXPAND);
        m_webview_created = true;
        Layout();
    }
    else
    {
        BOOST_LOG_TRIVIAL(error) << "PrinterWebViewPanel: Failed to create webview";
    }
}

void PrinterWebViewPanel::LoadURL(const wxString &url)
{
    if (!m_webview || url.empty())
        return;

    m_current_url = url;

    // Build URL with authentication if needed
    wxString auth_url = BuildAuthenticatedURL(url);

    // Log the original URL (not the authenticated one which may contain credentials)
    BOOST_LOG_TRIVIAL(info) << "PrinterWebViewPanel: Loading URL: " << url.ToStdString();
    m_webview->LoadURL(auth_url);
}

wxString PrinterWebViewPanel::BuildAuthenticatedURL(const wxString &url)
{
    // For HTTP Basic/Digest auth, we can embed credentials in the URL
    // Note: This is not ideal for security, but wxWebView doesn't expose
    // a way to set custom headers for all requests.
    // For API key auth, we'd need to inject JavaScript or use a different approach.

    if (!m_user.empty() && !m_password.empty())
    {
        wxURI uri(url);
        wxString scheme = uri.GetScheme();
        if (scheme.empty())
            scheme = "http";

        wxString server = uri.GetServer();
        wxString path = uri.GetPath();
        wxString port_str;
        if (uri.HasPort())
            port_str = wxString::Format(":%s", uri.GetPort());

        // Construct URL with embedded credentials
        // Format: scheme://user:password@host:port/path
        return wxString::Format("%s://%s:%s@%s%s%s", scheme, from_u8(m_user), from_u8(m_password), server, port_str,
                                path);
    }

    // For API key auth or no auth, just return the URL as-is
    // Note: API key auth typically requires header injection which isn't
    // directly supported by wxWebView for all requests
    return url;
}

void PrinterWebViewPanel::SetAPIKey(const std::string &key)
{
    m_api_key = key;
    // Note: wxWebView doesn't directly support setting custom headers for all requests
    // Some printer interfaces may work without the API key for viewing
    // Full API key support would require custom request handling
}

void PrinterWebViewPanel::SetCredentials(const std::string &user, const std::string &password)
{
    m_user = user;
    m_password = password;
}

void PrinterWebViewPanel::Reload()
{
    if (m_webview)
    {
        if (!m_current_url.empty())
        {
            // Reload with authentication
            LoadURL(m_current_url);
        }
        else
        {
            m_webview->Reload();
        }
    }
}

bool PrinterWebViewPanel::IsLoaded() const
{
    return m_webview_created && m_webview != nullptr;
}

wxString PrinterWebViewPanel::GetCurrentURL() const
{
    return m_current_url;
}

void PrinterWebViewPanel::sys_color_changed()
{
    // Webview handles its own theming based on the webpage content
    // Nothing specific needed here
}

} // namespace GUI
} // namespace Slic3r
