///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <wx/panel.h>
#include <string>
#include <functional>

class wxWebView;
class wxBoxSizer;

namespace DSKY
{

// A panel that hosts an embedded wxWebView for displaying printer web interfaces
class PrinterWebViewPanel : public wxPanel
{
public:
    PrinterWebViewPanel(wxWindow *parent);
    ~PrinterWebViewPanel();

    // Load a URL in the webview
    void LoadURL(const wxString &url);

    // Set API key authentication (for OctoPrint-style hosts)
    void SetAPIKey(const std::string &key);

    // Set username/password authentication (for HTTP Digest auth)
    void SetCredentials(const std::string &user, const std::string &password);

    // Reload the current page
    void Reload();

    // Check if webview is initialized and has loaded content
    bool IsLoaded() const;

    // Get the current URL
    wxString GetCurrentURL() const;

    // Notify the panel it is now visible; triggers a one-time refresh on
    // Linux to work around a WebKit2GTK stale-view quirk.
    void OnBecameVisible();

    // Handle system color changes (dark mode)
    void sys_color_changed();

    // Receives the reason when the webview's content process dies; the panel is dead from
    // then on and its owner tears it down. Only backends with a separate content process
    // (WebKit2GTK) ever call it.
    void SetTerminationHandler(std::function<void(const std::string &reason)> handler);

private:
    void CreateWebView();
    wxString BuildAuthenticatedURL(const wxString &url);

    wxWebView *m_webview{nullptr};
    wxBoxSizer *m_sizer{nullptr};

    wxString m_current_url;
    std::string m_api_key;
    std::string m_user;
    std::string m_password;
    bool m_webview_created{false};

    // Some printer web interfaces (e.g. Mainsail) initially load a read-only
    // cached view on first load.  A single hard-refresh after the panel becomes
    // visible resolves this; the flag tracks whether we still need to do it.
    bool m_needs_initial_refresh{false};
};

} // namespace DSKY
