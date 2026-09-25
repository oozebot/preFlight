///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <wx/webview.h>
#include <string>
#include <atomic>
#include <functional>
namespace DSKY
{

void setup_webview_with_credentials(wxWebView *web_view, const std::string &username, const std::string &password);
void remove_webview_credentials(wxWebView *web_view);
void delete_cookies(wxWebView *web_view, const std::string &url);
void delete_cookies_with_counter(wxWebView *web_view, const std::string &url, std::atomic<size_t> &counter);
void add_request_authorization(wxWebView *web_view, const wxString &address, const std::string &token);
void remove_request_authorization(wxWebView *web_view);
void load_request(wxWebView *web_view, const std::string &address, const std::string &token);
// Calls on_terminated when the content process behind the view dies; reason is a short token
// for the log. The call comes from the backend's signal dispatch on the GUI thread, so the
// receiver defers any teardown of the view. Backends without a separate content process never
// call it.
void setup_webview_termination_handler(wxWebView *web_view,
                                       std::function<void(const std::string &reason)> on_terminated);
} // namespace DSKY
