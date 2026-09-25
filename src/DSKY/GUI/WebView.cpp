///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "WebView.hpp"
#include "DSKY/GUI/GUI_App.hpp"
#include "DSKY/GUI/GUI.hpp"
#include "DSKY/GUI/format.hpp"

#include "luminary/platform/host/Platform.hpp"

#include <wx/uri.h>
#include <wx/webview.h>

#include <boost/log/trivial.hpp>

wxWebView *WebView::webview_new()
{
#if wxUSE_WEBVIEW_EDGE
    bool backend_available = wxWebView::IsBackendAvailable(wxWebViewBackendEdge);
#else
    bool backend_available = wxWebView::IsBackendAvailable(wxWebViewBackendWebKit);
#endif

    wxWebView *webView = nullptr;
    if (backend_available)
        webView = wxWebView::New();
    if (!webView)
        BOOST_LOG_TRIVIAL(error) << "Failed to create wxWebView object.";
    return webView;
}
void WebView::webview_create(wxWebView *webView, wxWindow *parent, const wxString &url,
                             const std::vector<std::string> &message_handlers)
{
    assert(webView);
    wxString correct_url = url.empty() ? wxString("") : wxURI(url).BuildURI();
    wxString user_agent = DSKY::format_wxstr("%1%/%2% (%3%)", PREFLIGHT_APP_FULL_NAME, PREFLIGHT_VERSION,
                                             Luminary::platform_to_string(Luminary::platform()));
#ifdef __WIN32__
    webView->SetUserAgent(user_agent);
    webView->Create(parent, wxID_ANY, correct_url, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
    //We register the wxfs:// protocol for testing purposes
    //webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    //And the memory: file system
    //webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#else
    // With WKWebView handlers need to be registered before creation
    //webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    // And the memory: file system
    //webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
    webView->Create(parent, wxID_ANY, correct_url, wxDefaultPosition, wxDefaultSize);
    webView->SetUserAgent(user_agent);
#endif
#ifndef __WIN32__
    DSKY::wxGetApp().CallAfter(
        [message_handlers, webView]
        {
#endif
            for (const std::string &handler : message_handlers)
            {
                if (!webView->AddScriptMessageHandler(DSKY::from_u8(handler)))
                {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Could not add script message handler " << handler;
                }
            }
#ifndef __WIN32__
        });
#endif
    webView->EnableContextMenu(true);
}