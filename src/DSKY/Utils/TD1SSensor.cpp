///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "TD1SSensor.hpp"
#include "Serial.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <wx/app.h>

namespace Luminary
{

TD1SSensor::~TD1SSensor()
{
    stop();
}

void TD1SSensor::start(ReadingCallback callback)
{
    // Join any previous thread before starting a new one
    if (m_thread.joinable())
    {
        m_stop_requested.store(true);
        m_thread.join();
    }

    m_callback = std::move(callback);
    m_stop_requested.store(false);
    m_running.store(true);
    m_alive = std::make_shared<std::atomic<bool>>(true);
    m_dialog_active = std::make_shared<std::atomic<bool>>(false);

    m_thread = std::thread(&TD1SSensor::monitor_thread_func, this);
}

void TD1SSensor::stop()
{
    // Signal shutdown to any pending CallAfter lambdas
    if (m_alive)
        m_alive->store(false);

    m_stop_requested.store(true);

    // Close the serial handle to unblock any pending read.
    // The read_loop holds m_port_mutex during ReadFile, so we must
    // use CancelIoEx first (which doesn't need the mutex) to interrupt
    // the blocking read, then the read will fail and release the lock.
#ifdef _WIN32
    {
        // CancelIoEx can be called without holding the mutex - it just
        // signals the kernel to abort pending I/O on this handle.
        std::lock_guard<std::mutex> lock(m_port_mutex);
        if (m_serial_handle && m_serial_handle != INVALID_HANDLE_VALUE)
            CancelIoEx(m_serial_handle, nullptr);
    }
#endif

    if (m_thread.joinable())
        m_thread.join();

    // Clean close after thread has exited
    close_port();

    m_running.store(false);
    m_connected.store(false);
}

std::string TD1SSensor::port_name() const
{
    std::lock_guard<std::mutex> lock(m_port_mutex);
    return m_port_name;
}

void TD1SSensor::monitor_thread_func()
{
    while (!m_stop_requested.load())
    {
        if (!m_connected.load())
        {
            if (try_connect())
            {
                m_connected.store(true);
                read_loop();
                // read_loop exited - device disconnected or stop requested
                m_connected.store(false);
                close_port();
                {
                    std::lock_guard<std::mutex> lock(m_port_mutex);
                    m_port_name.clear();
                }
            }
            else
            {
                // Device not found - wait before scanning again
                for (int i = 0; i < 50 && !m_stop_requested.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

bool TD1SSensor::open_port(const std::string &port_name_str)
{
#ifdef _WIN32
    std::string device_path = "\\\\.\\" + port_name_str;
    HANDLE h = CreateFileA(device_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb))
    {
        CloseHandle(h);
        return false;
    }
    dcb.BaudRate = BAUD_RATE;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb))
    {
        CloseHandle(h);
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 1000;
    SetCommTimeouts(h, &timeouts);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    {
        std::lock_guard<std::mutex> lock(m_port_mutex);
        m_serial_handle = h;
        m_port_name = port_name_str;
    }
    return true;
#else
    int fd = open(port_name_str.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return false;

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tio = {};
    tcgetattr(fd, &tio);
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
    tio.c_cflag = CS8 | CREAD | CLOCAL;
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 10;
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);

    {
        std::lock_guard<std::mutex> lock(m_port_mutex);
        m_serial_fd = fd;
        m_port_name = port_name_str;
    }
    return true;
#endif
}

void TD1SSensor::close_port()
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(m_port_mutex);
    if (m_serial_handle && m_serial_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_serial_handle);
        m_serial_handle = nullptr;
    }
#else
    std::lock_guard<std::mutex> lock(m_port_mutex);
    if (m_serial_fd >= 0)
    {
        close(m_serial_fd);
        m_serial_fd = -1;
    }
#endif
}

bool TD1SSensor::try_connect()
{
    auto ports = Utils::scan_serial_ports_extended();

    for (const auto &port : ports)
    {
        if (m_stop_requested.load())
            return false;

        if (port.id_match(VID, PID))
        {
            if (open_port(port.port))
                return true;
        }
    }

    return false;
}

void TD1SSensor::read_loop()
{
    std::string line_buffer;

    while (!m_stop_requested.load())
    {
        char c = 0;
        bool got_char = false;

#ifdef _WIN32
        {
            // Hold the mutex during the entire ReadFile call so stop()
            // cannot close the handle while we're using it.
            std::lock_guard<std::mutex> lock(m_port_mutex);
            if (!m_serial_handle || m_serial_handle == INVALID_HANDLE_VALUE)
                return;
            DWORD bytes_read = 0;
            BOOL ok = ReadFile(m_serial_handle, &c, 1, &bytes_read, nullptr);
            if (!ok)
                return; // CancelIoEx fired or device disconnected
            got_char = (bytes_read == 1);
        }
#else
        {
            std::lock_guard<std::mutex> lock(m_port_mutex);
            if (m_serial_fd < 0)
                return;
            ssize_t n = read(m_serial_fd, &c, 1);
            if (n < 0)
                return;
            got_char = (n == 1);
        }
#endif

        if (!got_char)
            continue;

        if (c == '\n' || c == '\r')
        {
            if (!line_buffer.empty())
            {
                TD1SReading reading;
                if (parse_line(line_buffer, reading) && m_callback)
                {
                    // Only dispatch if no dialog is already showing
                    if (!m_dialog_active || !m_dialog_active->load())
                    {
                        TD1SReading captured = reading;
                        auto cb = m_callback;
                        auto alive = m_alive;
                        auto dialog_flag = m_dialog_active; // Capture shared_ptr by value

                        wxTheApp->CallAfter(
                            [cb, captured, alive, dialog_flag]()
                            {
                                // Check that the sensor (and app) are still alive
                                if (!alive || !alive->load())
                                    return;

                                dialog_flag->store(true);
                                cb(captured);
                                dialog_flag->store(false);
                            });
                    }
                }
                line_buffer.clear();
            }
        }
        else
        {
            // Cap line buffer to prevent memory exhaustion from malformed data
            if (line_buffer.size() < MAX_LINE_LENGTH)
                line_buffer += c;
            else
                line_buffer.clear(); // Discard oversized line
        }
    }
}

// Parse: "44065768853,,,,4.5,B0443E"
bool TD1SSensor::parse_line(const std::string &line, TD1SReading &out)
{
    if (line.find(',') == std::string::npos)
        return false;

    std::vector<std::string> fields;
    std::istringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ','))
        fields.push_back(field);

    if (fields.size() < 6)
        return false;

    const std::string &td_str = fields[4];
    if (td_str.empty())
        return false;

    float td = 0.0f;
    try
    {
        td = std::stof(td_str);
    }
    catch (...)
    {
        return false;
    }

    if (td <= 0.0f || td > 100.0f)
        return false;

    const std::string &hex_str = fields[5];
    std::string hex = hex_str;
    hex.erase(std::remove_if(hex.begin(), hex.end(), ::isspace), hex.end());

    if (hex.size() != 6)
        return false;

    unsigned int r, g, b;
    if (sscanf(hex.c_str(), "%02x%02x%02x", &r, &g, &b) != 3)
        return false;

    out.color = ColorRGB(r / 255.0f, g / 255.0f, b / 255.0f);
    out.td = td;
    out.hex_color = hex;

    return true;
}

} // namespace Luminary
