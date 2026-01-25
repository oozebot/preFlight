///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/webview.h>
#include <string>
#include <atomic>
namespace Slic3r::GUI
{
void setup_webview_with_credentials(wxWebView *web_view, const std::string &username, const std::string &password);
void remove_webview_credentials(wxWebView *web_view);
void delete_cookies(wxWebView *web_view, const std::string &url);
void delete_cookies_with_counter(wxWebView *web_view, const std::string &url, std::atomic<size_t> &counter);
void add_request_authorization(wxWebView *web_view, const wxString &address, const std::string &token);
void remove_request_authorization(wxWebView *web_view);
void load_request(wxWebView *web_view, const std::string &address, const std::string &token);
} // namespace Slic3r::GUI
