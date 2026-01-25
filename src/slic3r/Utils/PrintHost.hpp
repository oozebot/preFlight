///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_PrintHost_hpp_
#define slic3r_PrintHost_hpp_

#include <memory>
#include <set>
#include <string>
#include <functional>
#include <boost/filesystem/path.hpp>

#include <wx/string.h>

#include <libslic3r/enum_bitmask.hpp>
#include "Http.hpp"

class wxArrayString;

namespace Slic3r
{

class DynamicPrintConfig;

enum class PrintHostPostUploadAction
{
    None,
    StartPrint,
    StartSimulation,
    QueuePrint
};
using PrintHostPostUploadActions = enum_bitmask<PrintHostPostUploadAction>;
ENABLE_ENUM_BITMASK_OPERATORS(PrintHostPostUploadAction);

struct PrintHostUpload
{
    boost::filesystem::path source_path;
    boost::filesystem::path upload_path;

    std::string group;
    std::string storage;

    PrintHostPostUploadAction post_action{PrintHostPostUploadAction::None};

    std::string data_json;
};

class PrintHost
{
public:
    virtual ~PrintHost();

    typedef Http::ProgressFn ProgressFn;
    typedef std::function<void(wxString /* error */)> ErrorFn;
    typedef std::function<void(wxString /* tag */, wxString /* status */)> InfoFn;

    virtual const char *get_name() const = 0;

    virtual bool test(wxString &curl_msg) const = 0;
    virtual wxString get_test_ok_msg() const = 0;
    virtual wxString get_test_failed_msg(wxString &msg) const = 0;
    virtual bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn,
                        InfoFn info_fn) const = 0;
    virtual bool has_auto_discovery() const = 0;
    virtual bool can_test() const = 0;
    virtual PrintHostPostUploadActions get_post_upload_actions() const = 0;
    // A print host usually does not support multiple printers, with the exception of Repetier server.
    virtual bool supports_multiple_printers() const { return false; }
    virtual std::string get_host() const = 0;
    virtual std::string get_notification_host() const { return get_host(); }

    // Support for Repetier server multiple groups & printers. Not supported by other print hosts.
    // Returns false if not supported. May throw HostNetworkError.
    virtual bool get_groups(wxArrayString & /* groups */) const { return false; }
    virtual bool get_printers(wxArrayString & /* printers */) const { return false; }
    // Support for LocalLink uploading to different storage. Not supported by other print hosts.
    // Returns false if not supported or fail.
    virtual bool get_storage(wxArrayString & /*storage_path*/, wxArrayString & /*storage_name*/) const { return false; }
    virtual std::string get_unusable_symbols() const { return {}; }

    // Returns false if not supported. Fills the config strings with formatted M-codes.
    // Only Duet/RRF printers support this feature.
    struct MachineLimitsResult
    {
        std::string m566; // Jerk: "M566 X600 Y600 Z600 E3600 P1"
        std::string m201; // Max accel: "M201 X6000 Y6000 Z1200 E6000"
        std::string m203; // Max speed: "M203 X24000 Y24000 Z3000 E6000"
        std::string m204; // Print/travel accel (optional): "M204 P600 T6000"
        std::string m207; // Firmware retract (optional): "M207 S0.80 R-0.05 F4500 T4500 Z1.00"
    };
    virtual bool get_machine_limits(wxString &msg, MachineLimitsResult &result) const { return false; }

    static PrintHost *get_print_host(DynamicPrintConfig *config);

protected:
    virtual wxString format_error(const std::string &body, const std::string &error, unsigned status) const;
};

struct PrintHostJob
{
    PrintHostUpload upload_data;
    std::unique_ptr<PrintHost> printhost;
    bool cancelled = false;

    PrintHostJob() {}
    PrintHostJob(const PrintHostJob &) = delete;
    PrintHostJob(PrintHostJob &&other)
        : upload_data(std::move(other.upload_data)), printhost(std::move(other.printhost)), cancelled(other.cancelled)
    {
    }

    PrintHostJob(DynamicPrintConfig *config) : printhost(PrintHost::get_print_host(config)) {}

    PrintHostJob &operator=(const PrintHostJob &) = delete;
    PrintHostJob &operator=(PrintHostJob &&other)
    {
        upload_data = std::move(other.upload_data);
        printhost = std::move(other.printhost);
        cancelled = other.cancelled;
        return *this;
    }

    bool empty() const { return !printhost; }
    operator bool() const { return !!printhost; }
};

namespace GUI
{
class PrintHostQueueDialog;
}

class PrintHostJobQueue
{
public:
    PrintHostJobQueue(GUI::PrintHostQueueDialog *queue_dialog);
    PrintHostJobQueue(const PrintHostJobQueue &) = delete;
    PrintHostJobQueue(PrintHostJobQueue &&other) = delete;
    ~PrintHostJobQueue();

    PrintHostJobQueue &operator=(const PrintHostJobQueue &) = delete;
    PrintHostJobQueue &operator=(PrintHostJobQueue &&other) = delete;

    void enqueue(PrintHostJob job);
    void cancel(size_t id);

private:
    struct priv;
    std::shared_ptr<priv> p;
};

} // namespace Slic3r

#endif
