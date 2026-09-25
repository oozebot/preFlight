///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <vector>
#include <string>

class wxWebView;
class wxWindow;
class wxString;

namespace WebView
{
wxWebView *webview_new();
void webview_create(wxWebView *webview, wxWindow *parent, const wxString &url,
                    const std::vector<std::string> &message_handlers);
}; // namespace WebView

