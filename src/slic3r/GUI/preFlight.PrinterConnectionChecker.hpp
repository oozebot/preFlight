///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_GUI_PrinterConnectionChecker_hpp_
#define slic3r_GUI_PrinterConnectionChecker_hpp_

#include <wx/event.h>
#include <wx/timer.h>
#include <functional>
#include <atomic>
#include <string>
#include <memory>

namespace Slic3r
{

class DynamicPrintConfig;

namespace GUI
{

// Async connectivity checker for physical printers
// Uses wxTimer for periodic polling and Http for non-blocking requests
class PrinterConnectionChecker : public wxEvtHandler
{
public:
    enum class State
    {
        Unknown, // Initial state, no check performed yet
        Online,  // Printer responded successfully
        Offline  // Printer did not respond or error occurred
    };

    // Callback function type - called when connection state changes
    using StateCallback = std::function<void(State)>;

    PrinterConnectionChecker(StateCallback callback);
    ~PrinterConnectionChecker();

    // Set the printer configuration to check
    // Should contain print_host, printhost_apikey, printhost_user, printhost_password,
    // printhost_authorization_type fields
    void SetPrinterConfig(const DynamicPrintConfig *config);

    // Start periodic polling at the specified interval (default 20 seconds)
    void StartPolling(unsigned int intervalMs = 20000);

    // Stop periodic polling
    void StopPolling();

    // Perform an immediate connectivity check
    void CheckNow();

    // Get the current connection state
    State GetState() const { return m_state; }

    // Check if currently polling
    bool IsPolling() const { return m_polling; }

private:
    void OnTimer(wxTimerEvent &event);
    void PerformCheck();
    void OnCheckComplete(bool success);

    wxTimer m_timer;
    StateCallback m_callback;
    const DynamicPrintConfig *m_config{nullptr};
    State m_state{State::Unknown};
    std::atomic<bool> m_check_in_progress{false};
    bool m_polling{false};

    // Cached config values for async access
    std::string m_host;
    std::string m_apikey;
    std::string m_user;
    std::string m_password;
    int m_auth_type{0}; // 0 = API key, 1 = user/password

    // Cached DNS resolution to avoid repeated DNS lookups
    std::string m_cached_ip;
    unsigned m_cached_port{0};

    // Shared flag to detect if object is still alive when async callbacks fire
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PrinterConnectionChecker_hpp_
