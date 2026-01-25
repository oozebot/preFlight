///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2022 David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Spencer Owen @spuder
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_Repetier_hpp_
#define slic3r_Repetier_hpp_

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>

#include "PrintHost.hpp"

namespace Slic3r
{

class DynamicPrintConfig;
class Http;

class Repetier : public PrintHost
{
public:
    Repetier(DynamicPrintConfig *config);
    ~Repetier() override = default;

    const char *get_name() const override;

    bool test(wxString &curl_msg) const override;
    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    bool has_auto_discovery() const override { return false; }
    bool can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override
    {
        return PrintHostPostUploadAction::StartPrint;
    }
    bool supports_multiple_printers() const override { return true; }
    std::string get_host() const override { return host; }

    bool get_groups(wxArrayString &groups) const override;
    bool get_printers(wxArrayString &printers) const override;

private:
    std::string host;
    std::string apikey;
    std::string cafile;
    std::string port;

    void set_auth(Http &http) const;
    std::string make_url(const std::string &path) const;
};

} // namespace Slic3r

#endif
