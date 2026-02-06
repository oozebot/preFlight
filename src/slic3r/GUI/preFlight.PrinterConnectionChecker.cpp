///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "preFlight.PrinterConnectionChecker.hpp"
#include "GUI_App.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r
{
namespace GUI
{

PrinterConnectionChecker::PrinterConnectionChecker(StateCallback callback) : m_callback(std::move(callback))
{
    m_timer.Bind(wxEVT_TIMER, &PrinterConnectionChecker::OnTimer, this);
}

PrinterConnectionChecker::~PrinterConnectionChecker()
{
    // Mark object as destroyed so async callbacks won't use dangling pointer
    *m_alive = false;
    StopPolling();
}

void PrinterConnectionChecker::SetPrinterConfig(const DynamicPrintConfig *config)
{
    m_config = config;

    // Cache config values for thread-safe access during async operations
    if (m_config)
    {
        m_host = m_config->opt_string("print_host");
        m_apikey = m_config->opt_string("printhost_apikey");
        m_user = m_config->opt_string("printhost_user");
        m_password = m_config->opt_string("printhost_password");

        // Get authorization type
        auto auth_opt = m_config->option<ConfigOptionEnum<AuthorizationType>>("printhost_authorization_type");
        m_auth_type = auth_opt ? static_cast<int>(auth_opt->value) : 0;
    }
    else
    {
        m_host.clear();
        m_apikey.clear();
        m_user.clear();
        m_password.clear();
        m_auth_type = 0;
    }

    // Reset state and DNS cache when config changes
    m_state = State::Unknown;
    m_cached_ip.clear();
    m_cached_port = 0;
}

void PrinterConnectionChecker::StartPolling(unsigned int intervalMs)
{
    if (m_polling)
        return;

    m_polling = true;

    // Perform an immediate check
    CheckNow();

    // Start the timer for periodic checks
    m_timer.Start(intervalMs);
}

void PrinterConnectionChecker::StopPolling()
{
    m_polling = false;
    m_timer.Stop();
}

void PrinterConnectionChecker::CheckNow()
{
    PerformCheck();
}

void PrinterConnectionChecker::OnTimer(wxTimerEvent & /*event*/)
{
    PerformCheck();
}

void PrinterConnectionChecker::PerformCheck()
{
    // Don't start another check if one is already in progress
    if (m_check_in_progress.exchange(true))
        return;

    if (m_host.empty())
    {
        OnCheckComplete(false);
        return;
    }

    // Build the URL and extract host/port for DNS caching
    std::string url = m_host;
    if (url.find("://") == std::string::npos)
    {
        url = "http://" + url;
    }
    // Remove trailing slash if present
    if (!url.empty() && url.back() == '/')
    {
        url.pop_back();
    }

    // Extract hostname and port from URL for DNS caching
    std::string host_for_dns;
    unsigned port_for_dns = 80;
    {
        // Parse URL to extract host and port
        // Format: scheme://host[:port]/path
        size_t scheme_end = url.find("://");
        if (scheme_end != std::string::npos)
        {
            size_t host_start = scheme_end + 3;
            size_t host_end = url.find('/', host_start);
            std::string host_port = (host_end != std::string::npos) ? url.substr(host_start, host_end - host_start)
                                                                    : url.substr(host_start);

            // Check for port
            size_t port_sep = host_port.find(':');
            if (port_sep != std::string::npos)
            {
                host_for_dns = host_port.substr(0, port_sep);
                port_for_dns = static_cast<unsigned>(std::stoi(host_port.substr(port_sep + 1)));
            }
            else
            {
                host_for_dns = host_port;
                // Default port based on scheme
                if (url.substr(0, 5) == "https")
                    port_for_dns = 443;
            }
        }
    }

    url += "/api/version";

    BOOST_LOG_TRIVIAL(debug) << "PrinterConnectionChecker: Checking " << url;

    auto http = Http::get(std::move(url));

    // Use cached DNS resolution if available to avoid DNS lookup
    if (!m_cached_ip.empty() && !host_for_dns.empty() && m_cached_port == port_for_dns)
    {
        http.resolve(host_for_dns, port_for_dns, m_cached_ip);
    }

    // Set authentication based on type
    // atKeyPassword = 0, atUserPassword = 1 (from PrintConfig.hpp AuthorizationType enum)
    if (m_auth_type == 0)
    {
        // API Key authentication
        if (!m_apikey.empty())
        {
            http.header("X-Api-Key", m_apikey);
        }
    }
    else
    {
        // HTTP Digest authentication
        if (!m_user.empty())
        {
            http.auth_digest(m_user, m_password);
        }
    }

    // Capture alive flag by value so we can check if object still exists when callback fires
    std::weak_ptr<bool> weak_alive = m_alive;

    // Capture host/port for DNS caching in the callback
    std::string captured_host = host_for_dns;
    unsigned captured_port = port_for_dns;

    http.timeout_connect(5) // 5 second connection timeout
        .timeout_max(10)    // 10 second total timeout
        .on_ip_resolve(
            [this, weak_alive, captured_host, captured_port](std::string ip_address)
            {
                // Cache the resolved IP for future requests (avoid repeated DNS lookups)
                wxGetApp().CallAfter(
                    [this, weak_alive, captured_host, captured_port, ip_address]()
                    {
                        auto alive = weak_alive.lock();
                        if (!alive || !*alive)
                            return;
                        if (!ip_address.empty() && !captured_host.empty())
                        {
                            // Only update cache if this is a new or changed IP
                            if (m_cached_ip != ip_address || m_cached_port != captured_port)
                            {
                                m_cached_ip = ip_address;
                                m_cached_port = captured_port;
                            }
                        }
                    });
            })
        .on_complete(
            [this, weak_alive](std::string body, unsigned status)
            {
                BOOST_LOG_TRIVIAL(debug) << "PrinterConnectionChecker: Got response, status=" << status;
                // Schedule UI update on main thread
                wxGetApp().CallAfter(
                    [this, weak_alive, status]()
                    {
                        // Check if object is still alive before accessing it
                        auto alive = weak_alive.lock();
                        if (!alive || !*alive)
                            return;
                        // Consider 2xx status codes as success
                        OnCheckComplete(status >= 200 && status < 300);
                    });
            })
        .on_error(
            [this, weak_alive](std::string body, std::string error, unsigned status)
            {
                BOOST_LOG_TRIVIAL(debug) << "PrinterConnectionChecker: Error - " << error << ", status=" << status;
                // Schedule UI update on main thread
                wxGetApp().CallAfter(
                    [this, weak_alive]()
                    {
                        // Check if object is still alive before accessing it
                        auto alive = weak_alive.lock();
                        if (!alive || !*alive)
                            return;
                        OnCheckComplete(false);
                    });
            })
        .perform(); // Async perform
}

void PrinterConnectionChecker::OnCheckComplete(bool success)
{
    m_check_in_progress = false;

    State new_state = success ? State::Online : State::Offline;

    // Only notify callback if state changed
    if (new_state != m_state)
    {
        m_state = new_state;
        if (m_callback)
        {
            m_callback(m_state);
        }
    }
}

} // namespace GUI
} // namespace Slic3r
