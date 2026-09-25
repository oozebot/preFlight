///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "luminary/colour/rgb/Color.hpp"

namespace Luminary
{

// Data from a single TD1S filament reading
struct TD1SReading
{
    ColorRGB color;
    float td = 0.0f;       // Transmission Distance in mm
    std::string hex_color; // Original hex string from device (e.g. "AB4139")
};

// Background monitor for the BIGTREETECH TD1S filament sensor.
// Detects the device by USB VID/PID, opens the serial port, and waits for
// filament insertion events. Each reading is delivered via a callback that
// is invoked on the UI thread (via wxCallAfter).
//
// Thread safety: start() and stop() must only be called from the UI thread.
class TD1SSensor
{
public:
    // TD1S USB identifiers
    static constexpr unsigned VID = 0xE4B2;
    static constexpr unsigned PID = 0x0045;
    static constexpr unsigned BAUD_RATE = 9600;

    // Max line length from device (protocol lines are ~30 chars)
    static constexpr size_t MAX_LINE_LENGTH = 256;

    using ReadingCallback = std::function<void(const TD1SReading &)>;

    TD1SSensor() = default;
    ~TD1SSensor();

    // Start the background monitor. The callback is invoked on the UI thread
    // (via wxCallAfter) whenever a new filament reading arrives.
    // Must be called from the UI thread.
    void start(ReadingCallback callback);

    // Stop monitoring and close the port.
    // Must be called from the UI thread.
    void stop();

    bool is_running() const { return m_running.load(); }
    bool is_connected() const { return m_connected.load(); }

    // Return the COM port currently in use (empty if not connected)
    std::string port_name() const;

private:
    void monitor_thread_func();
    bool try_connect();
    bool open_port(const std::string &port_name_str);
    void close_port();
    void read_loop();

    // Parse a CSV line from the device. Returns true if valid.
    static bool parse_line(const std::string &line, TD1SReading &out);

    ReadingCallback m_callback;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stop_requested{false};
    // Shared flags checked by CallAfter lambdas to avoid use-after-free on shutdown.
    // shared_ptr so the lambda's copy remains valid after TD1SSensor is destroyed.
    std::shared_ptr<std::atomic<bool>> m_alive;
    std::shared_ptr<std::atomic<bool>> m_dialog_active; // Prevents modal dialog stacking

    // Protects serial handle/fd. Held during read operations to prevent
    // stop() from closing the handle while a read is in progress.
    mutable std::mutex m_port_mutex;
    std::string m_port_name;

#ifdef _WIN32
    void *m_serial_handle = nullptr; // HANDLE, void* to avoid Windows.h in header
#else
    int m_serial_fd = -1;
#endif
};

} // namespace Luminary

