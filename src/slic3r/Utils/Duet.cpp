///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2020 Manuel Coenen
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Duet.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <thread>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/nowide/convert.hpp>

#include <wx/frame.h>
#include <wx/event.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/format.hpp"
#include "Http.hpp"

#include <curl/curl.h>

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

namespace Slic3r
{
namespace
{
std::string escape_string(const std::string &unescaped)
{
    std::string ret_val;
    CURL *curl = curl_easy_init();
    if (curl)
    {
        char *decoded = curl_easy_escape(curl, unescaped.c_str(), unescaped.size());
        if (decoded)
        {
            ret_val = std::string(decoded);
            curl_free(decoded);
        }
        curl_easy_cleanup(curl);
    }
    return ret_val;
}
} // namespace

Duet::Duet(DynamicPrintConfig *config)
    : host(config->opt_string("print_host")), password(config->opt_string("printhost_apikey"))
{
}

const char *Duet::get_name() const
{
    return "Duet";
}

bool Duet::test(wxString &msg) const
{
    auto connectionType = connect(msg);
    disconnect(connectionType);

    return connectionType != ConnectionType::error;
}

wxString Duet::get_test_ok_msg() const
{
    return _(L("Connection to Duet works correctly."));
}

wxString Duet::get_test_failed_msg(wxString &msg) const
{
    return GUI::format_wxstr("%s: %s", _L("Could not connect to Duet"), msg);
}

bool Duet::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    wxString connect_msg;
    auto connectionType = connect(connect_msg);
    if (connectionType == ConnectionType::error)
    {
        error_fn(std::move(connect_msg));
        return false;
    }

    bool res = true;
    bool dsf = (connectionType == ConnectionType::dsf);

    auto upload_cmd = get_upload_url(upload_data.upload_path.string(), connectionType);
    BOOST_LOG_TRIVIAL(info) << boost::format(
                                   "Duet: Uploading file %1%, filepath: %2%, post_action: %3%, command: %4%") %
                                   upload_data.source_path % upload_data.upload_path % int(upload_data.post_action) %
                                   upload_cmd;

    auto http = (dsf ? Http::put(std::move(upload_cmd)) : Http::post(std::move(upload_cmd)));
    if (dsf)
    {
        http.set_put_body(upload_data.source_path);
        // preFlight: Fix inverted condition - send session key when it IS present
        if (!connect_msg.empty())
            http.header("X-Session-Key", GUI::into_u8(connect_msg));
    }
    else
    {
        http.set_post_body(upload_data.source_path);
    }
    http.on_complete(
            [&](std::string body, unsigned status)
            {
                BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: File uploaded: HTTP %1%: %2%") % status % body;

                int err_code = dsf ? (status == 201 ? 0 : 1) : get_err_code_from_body(body);
                if (err_code != 0)
                {
                    BOOST_LOG_TRIVIAL(error)
                        << boost::format("Duet: Request completed but error code was received: %1%") % err_code;
                    error_fn(format_error(body, L("Unknown error occured"), 0));
                    res = false;
                }
                else if (upload_data.post_action == PrintHostPostUploadAction::StartPrint)
                {
                    wxString errormsg;
                    res = start_print(errormsg, upload_data.upload_path.string(), connectionType, false);
                    if (!res)
                    {
                        error_fn(std::move(errormsg));
                    }
                }
                else if (upload_data.post_action == PrintHostPostUploadAction::StartSimulation)
                {
                    wxString errormsg;
                    res = start_print(errormsg, upload_data.upload_path.string(), connectionType, true);
                    if (!res)
                    {
                        error_fn(std::move(errormsg));
                    }
                }
            })
        .on_error(
            [&](std::string body, std::string error, unsigned status)
            {
                BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Error uploading file: %1%, HTTP %2%, body: `%3%`") %
                                                error % status % body;
                error_fn(format_error(body, error, status));
                res = false;
            })
        .on_progress(
            [&](Http::Progress progress, bool &cancel)
            {
                prorgess_fn(std::move(progress), cancel);
                if (cancel)
                {
                    // Upload was canceled
                    BOOST_LOG_TRIVIAL(info) << "Duet: Upload canceled";
                    res = false;
                }
            })
        .perform_sync();

    disconnect(connectionType);

    return res;
}

Duet::ConnectionType Duet::connect(wxString &msg) const
{
    auto res = ConnectionType::error;
    auto url = get_connect_url(false);

    auto http = Http::get(std::move(url));
    http.on_error(
            [&](std::string body, std::string error, unsigned status)
            {
                auto dsfUrl = get_connect_url(true);
                auto dsfHttp = Http::get(std::move(dsfUrl));
                dsfHttp
                    .on_error(
                        [&](std::string body, std::string error, unsigned status)
                        {
                            BOOST_LOG_TRIVIAL(error)
                                << boost::format("Duet: Error connecting: %1%, HTTP %2%, body: `%3%`") % error %
                                       status % body;
                            msg = format_error(body, error, status);
                        })
                    .on_complete(
                        [&](std::string body, unsigned)
                        {
                            try
                            {
                                pt::ptree root;
                                std::istringstream iss(body);
                                pt::read_json(iss, root);
                                auto key = root.get_optional<std::string>("sessionKey");
                                if (key)
                                    msg = boost::nowide::widen(*key);
                                res = ConnectionType::dsf;
                            }
                            catch (const std::exception &)
                            {
                                BOOST_LOG_TRIVIAL(error)
                                    << "Failed to parse serverKey from Duet reply to Connect request: " << body;
                                msg = format_error(body, L("Failed to parse a Connect reply"), 0);
                                res = ConnectionType::error;
                            }
                        })
                    .perform_sync();
            })
        .on_complete(
            [&](std::string body, unsigned)
            {
                BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: Got: %1%") % body;

                int err_code = get_err_code_from_body(body);
                switch (err_code)
                {
                case 0:
                    res = ConnectionType::rrf;
                    break;
                case 1:
                    msg = format_error(body, L("Wrong password"), 0);
                    break;
                case 2:
                    msg = format_error(body, L("Could not get resources to create a new connection"), 0);
                    break;
                default:
                    msg = format_error(body, L("Unknown error occured"), 0);
                    break;
                }
            })
        .perform_sync();

    return res;
}

void Duet::disconnect(ConnectionType connectionType) const
{
    // we don't need to disconnect from DSF or if it failed anyway
    if (connectionType != ConnectionType::rrf)
    {
        return;
    }
    auto url = (boost::format("%1%rr_disconnect") % get_base_url()).str();

    auto http = Http::get(std::move(url));
    http.on_error(
            [&](std::string body, std::string error, unsigned status)
            {
                // we don't care about it, if disconnect is not working Duet will disconnect automatically after some time
                BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Error disconnecting: %1%, HTTP %2%, body: `%3%`") %
                                                error % status % body;
            })
        .perform_sync();
}

std::string Duet::get_upload_url(const std::string &filename, ConnectionType connectionType) const
{
    assert(connectionType != ConnectionType::error);

    if (connectionType == ConnectionType::dsf)
    {
        return (boost::format("%1%machine/file/gcodes/%2%") % get_base_url() % Http::url_encode(filename)).str();
    }
    else
    {
        return (boost::format("%1%rr_upload?name=0:/gcodes/%2%&%3%") % get_base_url() % Http::url_encode(filename) %
                timestamp_str())
            .str();
    }
}

std::string Duet::get_connect_url(const bool dsfUrl) const
{
    if (dsfUrl)
    {
        return (boost::format("%1%machine/connect?password=%2%") % get_base_url() %
                (password.empty() ? "reprap" : escape_string(password)))
            .str();
    }
    else
    {
        return (boost::format("%1%rr_connect?password=%2%&%3%") % get_base_url() %
                (password.empty() ? "reprap" : escape_string(password)) % timestamp_str())
            .str();
    }
}

std::string Duet::get_base_url() const
{
    if (host.find("http://") == 0 || host.find("https://") == 0)
    {
        if (host.back() == '/')
        {
            return host;
        }
        else
        {
            return (boost::format("%1%/") % host).str();
        }
    }
    else
    {
        return (boost::format("http://%1%/") % host).str();
    }
}

std::string Duet::timestamp_str() const
{
    enum
    {
        BUFFER_SIZE = 32
    };

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    char buffer[BUFFER_SIZE];
    std::strftime(buffer, BUFFER_SIZE, "time=%Y-%m-%dT%H:%M:%S", &tm);

    return std::string(buffer);
}

bool Duet::start_print(wxString &msg, const std::string &filename, ConnectionType connectionType,
                       bool simulationMode) const
{
    assert(connectionType != ConnectionType::error);

    bool res = false;
    bool dsf = (connectionType == ConnectionType::dsf);

    auto url = dsf ? (boost::format("%1%machine/code") % get_base_url()).str()
                   : (boost::format(simulationMode ? "%1%rr_gcode?gcode=M37%%20P\"0:/gcodes/%2%\""
                                                   : "%1%rr_gcode?gcode=M32%%20\"0:/gcodes/%2%\"") %
                      get_base_url() % Http::url_encode(filename))
                         .str();

    auto http = (dsf ? Http::post(std::move(url)) : Http::get(std::move(url)));
    if (dsf)
    {
        http.set_post_body(
            (boost::format(simulationMode ? "M37 P\"0:/gcodes/%1%\"" : "M32 \"0:/gcodes/%1%\"") % filename).str());
    }
    http.on_error(
            [&](std::string body, std::string error, unsigned status)
            {
                BOOST_LOG_TRIVIAL(error) << boost::format("Duet: Error starting print: %1%, HTTP %2%, body: `%3%`") %
                                                error % status % body;
                msg = format_error(body, error, status);
            })
        .on_complete(
            [&](std::string body, unsigned)
            {
                BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: Got: %1%") % body;
                res = true;
            })
        .perform_sync();

    return res;
}

int Duet::get_err_code_from_body(const std::string &body) const
{
    pt::ptree root;
    std::istringstream iss(body); // wrap returned json to istringstream
    pt::read_json(iss, root);

    return root.get<int>("err", 0);
}

bool Duet::send_gcode(const std::string &gcode, std::string &response, wxString &error_msg,
                      ConnectionType connectionType, const std::string &session_key) const
{
    if (connectionType == ConnectionType::error)
    {
        error_msg = _L("Not connected to printer");
        return false;
    }

    bool success = false;
    bool dsf = (connectionType == ConnectionType::dsf);

    // Build URL based on connection type
    auto url = dsf ? (boost::format("%1%machine/code") % get_base_url()).str()
                   : (boost::format("%1%rr_gcode?gcode=%2%") % get_base_url() % Http::url_encode(gcode)).str();

    auto http = dsf ? Http::post(std::move(url)) : Http::get(std::move(url));

    if (dsf)
    {
        http.set_post_body(gcode);
        // preFlight: Pass session key for authenticated DSF installations
        if (!session_key.empty())
            http.header("X-Session-Key", session_key);
    }

    http.on_error(
            [&](std::string body, std::string error, unsigned status)
            {
                BOOST_LOG_TRIVIAL(error) << boost::format(
                                                "Duet: Error sending gcode '%1%': %2%, HTTP %3%, body: `%4%`") %
                                                gcode % error % status % body;
                error_msg = format_error(body, error, status);
            })
        .on_complete(
            [&](std::string body, unsigned)
            {
                BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: GCode '%1%' response: %2%") % gcode % body;
                if (dsf)
                {
                    // DSF returns the G-code reply text inline
                    response = body;
                }
                success = true;
            })
        .perform_sync();

    // preFlight: For standalone RRF, rr_gcode returns only {"buff": N} (buffer status).
    // The actual G-code reply text must be fetched separately via GET /rr_reply.
    if (success && !dsf)
    {
        // Brief delay to let firmware process the command and buffer the reply
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        auto reply_url = (boost::format("%1%rr_reply") % get_base_url()).str();
        auto reply_http = Http::get(std::move(reply_url));
        reply_http
            .on_error(
                [&](std::string body, std::string error, unsigned status)
                {
                    BOOST_LOG_TRIVIAL(error)
                        << boost::format("Duet: Error fetching rr_reply for '%1%': %2%, HTTP %3%") % gcode % error %
                               status;
                })
            .on_complete(
                [&](std::string body, unsigned)
                {
                    BOOST_LOG_TRIVIAL(debug) << boost::format("Duet: rr_reply for '%1%': %2%") % gcode % body;
                    response = body;
                })
            .perform_sync();
    }

    return success;
}

// Parse M-code response and reconstruct the command
// Example input: "Maximum jerk rates (mm/min): X: 600.0, Y: 600.0, Z: 600.0, E: 3600.0, jerk policy: 1"
// Example output: "M566 X600 Y600 Z600 E3600 P1"
std::string Duet::parse_mcode_response(const std::string &response, const std::string &mcode) const
{
    // RRF returns responses in different formats depending on the command
    // We need to parse the text response and reconstruct the M-code

    std::string result;

    if (mcode == "M566")
    {
        // Parse: "Maximum jerk rates (mm/min): X: 600.0, Y: 600.0, Z: 600.0, E: 3600.0, jerk policy: 1"
        // Or DSF JSON response
        float x = 0, y = 0, z = 0, e = 0;
        int policy = 0;

        // Try to parse axis values
        auto parse_axis = [&response](const std::string &axis) -> float
        {
            std::string pattern = axis + ": ";
            size_t pos = response.find(pattern);
            if (pos != std::string::npos)
            {
                pos += pattern.length();
                return std::stof(response.substr(pos));
            }
            // Also try format "X600" without colon
            pattern = axis;
            pos = response.find(pattern);
            if (pos != std::string::npos && pos + 1 < response.length())
            {
                char next = response[pos + 1];
                if (next == ':' || next == ' ' || std::isdigit(next))
                {
                    pos++;
                    while (pos < response.length() && (response[pos] == ':' || response[pos] == ' '))
                        pos++;
                    if (pos < response.length())
                    {
                        return std::stof(response.substr(pos));
                    }
                }
            }
            return 0;
        };

        x = parse_axis("X");
        y = parse_axis("Y");
        z = parse_axis("Z");
        e = parse_axis("E");

        // Parse jerk policy
        size_t policy_pos = response.find("jerk policy:");
        if (policy_pos == std::string::npos)
            policy_pos = response.find("policy:");
        if (policy_pos == std::string::npos)
            policy_pos = response.find("P");
        if (policy_pos != std::string::npos)
        {
            size_t num_start = response.find_first_of("0123456789", policy_pos);
            if (num_start != std::string::npos)
            {
                policy = std::stoi(response.substr(num_start));
            }
        }

        if (x > 0 || y > 0 || z > 0 || e > 0)
        {
            result = (boost::format("M566 X%.0f Y%.0f Z%.0f E%.0f P%d") % x % y % z % e % policy).str();
        }
    }
    else if (mcode == "M201")
    {
        // Parse: "Accelerations (mm/s^2): X: 6000.0, Y: 6000.0, Z: 1200.0, E: 6000.0"
        float x = 0, y = 0, z = 0, e = 0;

        auto parse_axis = [&response](const std::string &axis) -> float
        {
            std::string pattern = axis + ": ";
            size_t pos = response.find(pattern);
            if (pos != std::string::npos)
            {
                pos += pattern.length();
                return std::stof(response.substr(pos));
            }
            return 0;
        };

        x = parse_axis("X");
        y = parse_axis("Y");
        z = parse_axis("Z");
        e = parse_axis("E");

        if (x > 0 || y > 0 || z > 0 || e > 0)
        {
            result = (boost::format("M201 X%.0f Y%.0f Z%.0f E%.0f") % x % y % z % e).str();
        }
    }
    else if (mcode == "M203")
    {
        // Parse: "Maximum speeds (mm/min): X: 24000.0, Y: 24000.0, Z: 3000.0, E: 6000.0"
        float x = 0, y = 0, z = 0, e = 0;

        auto parse_axis = [&response](const std::string &axis) -> float
        {
            std::string pattern = axis + ": ";
            size_t pos = response.find(pattern);
            if (pos != std::string::npos)
            {
                pos += pattern.length();
                return std::stof(response.substr(pos));
            }
            return 0;
        };

        x = parse_axis("X");
        y = parse_axis("Y");
        z = parse_axis("Z");
        e = parse_axis("E");

        if (x > 0 || y > 0 || z > 0 || e > 0)
        {
            result = (boost::format("M203 X%.0f Y%.0f Z%.0f E%.0f") % x % y % z % e).str();
        }
    }
    else if (mcode == "M204")
    {
        // Parse: "Maximum printing acceleration 600.0, maximum travel acceleration 6000.0 mm/sec^2"
        float p = 0, t = 0;

        // Look for "printing acceleration" followed by a number
        size_t print_pos = response.find("printing acceleration");
        if (print_pos != std::string::npos)
        {
            size_t num_start = print_pos + strlen("printing acceleration");
            while (num_start < response.length() && !std::isdigit(response[num_start]) && response[num_start] != '-')
                num_start++;
            if (num_start < response.length())
            {
                try
                {
                    p = std::stof(response.substr(num_start));
                }
                catch (...)
                {
                }
            }
        }

        // Look for "travel acceleration" followed by a number
        size_t travel_pos = response.find("travel acceleration");
        if (travel_pos != std::string::npos)
        {
            size_t num_start = travel_pos + strlen("travel acceleration");
            while (num_start < response.length() && !std::isdigit(response[num_start]) && response[num_start] != '-')
                num_start++;
            if (num_start < response.length())
            {
                try
                {
                    t = std::stof(response.substr(num_start));
                }
                catch (...)
                {
                }
            }
        }

        if (p > 0 || t > 0)
        {
            result = (boost::format("M204 P%.0f T%.0f") % p % t).str();
        }
    }
    else if (mcode == "M207")
    {
        // Parse: "Tool 0 retract/reprime: length 0.80/0.75mm, speed 75.0/75.0mm/sec, Z hop 1.00mm"
        // M207 is optional - may not be configured
        float s = 0, r = 0, f = 0, t = 0, z = 0;
        bool has_values = false;

        // Helper to extract number at position
        auto extract_number = [](const std::string &str, size_t start) -> float
        {
            while (start < str.length() && !std::isdigit(str[start]) && str[start] != '-' && str[start] != '.')
                start++;
            if (start < str.length())
            {
                try
                {
                    return std::stof(str.substr(start));
                }
                catch (...)
                {
                }
            }
            return 0;
        };

        // Parse "length X.XX/Y.YYmm" - X is retract length (S), Y is reprime length
        // R (extra restart) = reprime - retract
        size_t length_pos = response.find("length");
        if (length_pos != std::string::npos)
        {
            size_t num_start = length_pos + strlen("length");
            float retract_len = extract_number(response, num_start);

            // Find the slash to get reprime length
            size_t slash_pos = response.find('/', num_start);
            float reprime_len = retract_len; // Default to same if not found
            if (slash_pos != std::string::npos && slash_pos < response.find("mm", num_start))
            {
                reprime_len = extract_number(response, slash_pos + 1);
            }

            s = retract_len;
            r = reprime_len - retract_len; // Extra restart length
            has_values = true;
        }

        // Parse "speed X.XX/Y.YYmm/sec" - X is retract speed, Y is reprime speed (in mm/sec)
        // Need to convert to mm/min for M207 command
        size_t speed_pos = response.find("speed");
        if (speed_pos != std::string::npos)
        {
            size_t num_start = speed_pos + strlen("speed");
            float retract_speed = extract_number(response, num_start);

            // Find the slash to get reprime speed
            size_t slash_pos = response.find('/', num_start);
            float reprime_speed = retract_speed; // Default to same if not found
            if (slash_pos != std::string::npos)
            {
                // Make sure we're still in the speed section (before next comma)
                size_t comma_pos = response.find(',', num_start);
                if (comma_pos == std::string::npos || slash_pos < comma_pos)
                {
                    reprime_speed = extract_number(response, slash_pos + 1);
                }
            }

            // Convert mm/sec to mm/min
            f = retract_speed * 60.0f;
            t = reprime_speed * 60.0f;
            has_values = true;
        }

        // Parse "Z hop X.XXmm"
        size_t zhop_pos = response.find("Z hop");
        if (zhop_pos != std::string::npos)
        {
            z = extract_number(response, zhop_pos + strlen("Z hop"));
            has_values = true;
        }

        if (has_values)
        {
            result = (boost::format("M207 S%.2f R%.2f F%.0f T%.0f Z%.2f") % s % r % f % t % z).str();
        }
    }

    return result;
}

bool Duet::get_machine_limits(wxString &msg, MachineLimitsResult &result) const
{
    // Connect to printer
    wxString connect_msg;
    auto connectionType = connect(connect_msg);
    if (connectionType == ConnectionType::error)
    {
        msg = connect_msg;
        return false;
    }

    bool success = true;
    bool dsf = (connectionType == ConnectionType::dsf);
    std::string response;
    wxString error;

    // preFlight: Extract session key for DSF authentication
    std::string session_key = dsf ? GUI::into_u8(connect_msg) : std::string();

    // preFlight: For standalone RRF, rr_reply can return stale/wrong data due to race conditions
    // with other HTTP clients (e.g. Duet Web Control). We retry each query up to 3 times,
    // validating the response via parse_mcode_response(). DSF returns replies inline so no retry needed.
    const int max_attempts = dsf ? 1 : 3;

    // Send M566 (jerk) - required
    for (int attempt = 0; attempt < max_attempts; ++attempt)
    {
        if (send_gcode("M566", response, error, connectionType, session_key))
        {
            result.m566 = parse_mcode_response(response, "M566");
            if (!result.m566.empty())
                break;
        }
        else
        {
            success = false;
            break; // Connection error, don't retry
        }
    }

    // Send M201 (max acceleration) - required
    for (int attempt = 0; attempt < max_attempts; ++attempt)
    {
        if (send_gcode("M201", response, error, connectionType, session_key))
        {
            result.m201 = parse_mcode_response(response, "M201");
            if (!result.m201.empty())
                break;
        }
        else
        {
            success = false;
            break;
        }
    }

    // Send M203 (max feedrate) - required
    for (int attempt = 0; attempt < max_attempts; ++attempt)
    {
        if (send_gcode("M203", response, error, connectionType, session_key))
        {
            result.m203 = parse_mcode_response(response, "M203");
            if (!result.m203.empty())
                break;
        }
        else
        {
            success = false;
            break;
        }
    }

    // Send M204 (print/travel acceleration) - optional
    for (int attempt = 0; attempt < max_attempts; ++attempt)
    {
        if (send_gcode("M204", response, error, connectionType, session_key))
        {
            result.m204 = parse_mcode_response(response, "M204");
            if (!result.m204.empty())
                break;
        }
        else
            break;
    }

    // Send M207 (firmware retraction) - optional
    for (int attempt = 0; attempt < max_attempts; ++attempt)
    {
        if (send_gcode("M207", response, error, connectionType, session_key))
        {
            result.m207 = parse_mcode_response(response, "M207");
            if (!result.m207.empty())
                break;
        }
        else
            break;
    }

    disconnect(connectionType);

    if (!success)
    {
        msg = _L("Failed to retrieve some machine limits from printer. Check connection and try again.");
    }
    else if (result.m566.empty() && result.m201.empty() && result.m203.empty())
    {
        msg = _L("Could not parse machine limits response. The printer may not support this feature.");
        success = false;
    }

    return success;
}

} // namespace Slic3r
