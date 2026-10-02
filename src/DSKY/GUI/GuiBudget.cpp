///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "GuiBudget.hpp"

#include "GUI_App.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"

#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>

// The widgets the probe builds (run_probe)
#ifdef PREFLIGHT_TEST_HOOKS
#include "wxExtensions.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/CollapsibleSection.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/RowIcons.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/ThemedTextCtrl.hpp"

#include <boost/nowide/fstream.hpp>

#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/thread.h>
#include <wx/timer.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

namespace DSKY
{
namespace GuiBudget
{
namespace
{

struct Counts
{
    long gdi{0};
    long user{0};
};

Counts current_counts()
{
#ifdef _WIN32
    const HANDLE process = ::GetCurrentProcess();
    return {long(::GetGuiResources(process, GR_GDIOBJECTS)), long(::GetGuiResources(process, GR_USEROBJECTS))};
#else
    return {};
#endif
}

// Milliseconds since the process started, so a trace lines up with an external sampler's clock
long long now_ms()
{
#ifdef _WIN32
    FILETIME creation, exit_time, kernel_time, user_time, now;
    ::GetSystemTimeAsFileTime(&now);
    if (::GetProcessTimes(::GetCurrentProcess(), &creation, &exit_time, &kernel_time, &user_time))
    {
        ULARGE_INTEGER from, to;
        from.LowPart = creation.dwLowDateTime;
        from.HighPart = creation.dwHighDateTime;
        to.LowPart = now.dwLowDateTime;
        to.HighPart = now.dwHighDateTime;
        return (long long) ((to.QuadPart - from.QuadPart) / 10000);
    }
#endif
    static const auto start = std::chrono::steady_clock::now();
    return (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
        .count();
}

// The counters the exit block always lists, zeros included, whether or not their site ran, so a
// reader of the block can tell a counter that stayed at zero from one that was renamed
const char *const ALWAYS_LISTED[] = {"EXTRUDERS_CAPPED",
                                     "GUI_BUDGET_SOFT_CEILING",
                                     "GUI_QUESTION_DIALOG_AUTO_ANSWERED",
                                     "GUI_SCROLL_CONTENT_OVERFLOW",
                                     "GUI_SCROLL_UPDATE_UNSETTLED",
                                     "GUI_SHARED_BRUSH_FAILED",
                                     "OVERRIDES_BUILD_RESTARTED",
                                     "OVERRIDES_OPENED_BEFORE_PREBUILT",
                                     "OVERRIDES_SYNC_BUILD",
                                     "SIDEBAR_EXTRUDER_KEY_COLLISION"};

// The trace file and the peak sampler. Lines are flushed as they are written, so a run that ends
// in a crash still leaves every line up to it; a trace without the closing "_END" line is one.
class Trace
{
public:
    static Trace &inst()
    {
        static Trace trace;
        return trace;
    }

    bool on() const { return m_out != nullptr; }

    // Event lines (the sampler's peaks, the soft ceiling) leave the delta base alone, so a snapshot's
    // delta stays the cost of what was built since the previous snapshot
    void line(const std::string &tag, const Counts &counts, const long long *value, bool event = false)
    {
        note_peak(counts);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_out == nullptr)
            return;
        std::fprintf(m_out, "%lld\t%s\t%ld\t%ld\t%ld\t%ld\t", now_ms(), tag.c_str(), counts.gdi, counts.user,
                     counts.gdi - m_last.gdi, counts.user - m_last.user);
        if (value != nullptr)
            std::fprintf(m_out, "%lld", *value);
        std::fputc('\n', m_out);
        std::fflush(m_out);
        if (!event)
            m_last = counts;
    }

    void note_peak(const Counts &counts)
    {
        raise_to(m_peak_gdi, counts.gdi);
        raise_to(m_peak_user, counts.user);
    }

    void finish()
    {
        stop_sampler();
        const Counts counts = current_counts();
        note_peak(counts);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_out == nullptr)
            return;
        std::fprintf(m_out, "peak_gdi\t%ld\npeak_user\t%ld\nexit_gdi\t%ld\nexit_user\t%ld\n", m_peak_gdi.load(),
                     m_peak_user.load(), counts.gdi, counts.user);
        // Every registered counter by name, the always-listed ones among them
        for (const char *name : ALWAYS_LISTED)
            ::Luminary::DbgCounters::inst().reg(name, true);
        auto values = ::Luminary::DbgCounters::inst().values();
        std::sort(values.begin(), values.end());
        for (const auto &[name, value] : values)
            std::fprintf(m_out, "[COUNTER] %s=%llu\n", name.c_str(), (unsigned long long) value);
        std::fprintf(m_out, "_END\n");
        std::fclose(m_out);
        m_out = nullptr;
    }

    ~Trace() { stop_sampler(); }

private:
    Trace()
    {
        const char *path = std::getenv("PREFLIGHT_GUI_BUDGET");
        if (path == nullptr || *path == '\0')
            return;
        m_out = boost::nowide::fopen(path, "w");
        if (m_out == nullptr)
        {
            BOOST_LOG_TRIVIAL(error) << "PREFLIGHT_GUI_BUDGET: cannot write " << path;
            return;
        }
        std::fprintf(m_out, "t_ms\ttag\tgdi\tuser\tdelta_gdi\tdelta_user\tvalue\n");
        std::fflush(m_out);
#ifdef _WIN32
        // The snapshots only see the counts at their own sites; the sampler sees the peaks between them,
        // a synchronous page build included. A rise of PEAK_STEP in either peak is written as a
        // "sampler.peak" line, so a run that crashes or is stopped still records how high it went.
        m_sampler = std::thread(
            [this]()
            {
                constexpr long PEAK_STEP = 250;
                Counts written;
                std::unique_lock<std::mutex> lock(m_stop_mutex);
                while (!m_stop_cv.wait_for(lock, std::chrono::milliseconds(20), [this]() { return m_stop; }))
                {
                    const Counts counts = current_counts();
                    note_peak(counts);
                    check_soft_ceiling(counts);
                    if (m_peak_gdi.load() >= written.gdi + PEAK_STEP || m_peak_user.load() >= written.user + PEAK_STEP)
                    {
                        written = {m_peak_gdi.load(), m_peak_user.load()};
                        line("sampler.peak", counts, nullptr, true);
                    }
                }
            });
#endif
    }

    void stop_sampler()
    {
        {
            std::lock_guard<std::mutex> lock(m_stop_mutex);
            m_stop = true;
        }
        m_stop_cv.notify_all();
        if (m_sampler.joinable())
            m_sampler.join();
    }

    static void raise_to(std::atomic<long> &peak, long value)
    {
        long seen = peak.load();
        while (value > seen && !peak.compare_exchange_weak(seen, value))
        {
        }
    }

public:
    static void check_soft_ceiling(const Counts &counts);

private:
    std::mutex m_mutex;
    FILE *m_out{nullptr};
    Counts m_last;
    std::atomic<long> m_peak_gdi{0};
    std::atomic<long> m_peak_user{0};
    std::thread m_sampler;
    std::mutex m_stop_mutex;
    std::condition_variable m_stop_cv;
    bool m_stop{false};
};

void Trace::check_soft_ceiling(const Counts &counts)
{
    static std::atomic<bool> above{false};
    const bool now_above = counts.gdi >= SOFT_CEILING || counts.user >= SOFT_CEILING;
    if (!now_above)
    {
        above.store(false);
        return;
    }
    if (above.exchange(true))
        return;
    DBG_COUNT_LOAD("GUI_BUDGET_SOFT_CEILING");
    BOOST_LOG_TRIVIAL(error) << "GUI budget: " << counts.gdi << " GDI and " << counts.user
                             << " USER objects, at or past the soft ceiling of " << SOFT_CEILING;
    Trace::inst().line("soft_ceiling", counts, nullptr, true);
}

} // namespace

bool tracing()
{
    return Trace::inst().on();
}

void snapshot(const std::string &tag)
{
    const Counts counts = current_counts();
    Trace::check_soft_ceiling(counts);
    if (Trace::inst().on())
        Trace::inst().line(tag, counts, nullptr);
}

void measure(const std::string &tag, long long value)
{
    const Counts counts = current_counts();
    Trace::check_soft_ceiling(counts);
    if (Trace::inst().on())
        Trace::inst().line(tag, counts, &value);
}

void content_height(const std::string &tag, int height, bool &overflowing)
{
    if (!tag.empty() && Trace::inst().on())
        measure(tag + ".content_height", height);
    // No scrolled content may pass the window coordinate range (the stacked extruder sections did):
    // counted and logged once per crossing
    const bool over = height > SCROLL_CONTENT_LIMIT;
    if (over && !overflowing)
    {
        DBG_COUNT_LOAD("GUI_SCROLL_CONTENT_OVERFLOW");
        const std::string name = tag.empty() ? std::string("scroll") : tag;
        BOOST_LOG_TRIVIAL(error) << "Scrolled content of " << name << " is " << height
                                 << " px tall, past the window coordinate range";
        if (Trace::inst().on())
            measure(name + ".overflow", height);
    }
    overflowing = over;
}

#ifdef PREFLIGHT_TEST_HOOKS
void dialog_dismissed(const std::string &title, bool question)
{
    // One line per dialog: tabs and line breaks in a title would split the trace's columns
    std::string flat = title;
    for (char &c : flat)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    if (question)
    {
        DBG_COUNT_LOAD("GUI_QUESTION_DIALOG_AUTO_ANSWERED");
        BOOST_LOG_TRIVIAL(error) << "PREFLIGHT_AUTO_DISMISS_DIALOGS declined a question (Cancel, else No): " << flat;
    }
    else
        BOOST_LOG_TRIVIAL(warning) << "PREFLIGHT_AUTO_DISMISS_DIALOGS answered a notice: " << flat;
    if (Trace::inst().on())
        Trace::inst().line(std::string(question ? "dialog.question: " : "dialog.notice: ") + flat, current_counts(),
                           nullptr, true);
}
#endif

void overrides_built_on_open()
{
    DBG_COUNT_LOAD("OVERRIDES_OPENED_BEFORE_PREBUILT");
    BOOST_LOG_TRIVIAL(error) << "Override panel opened before its rows were built";
    if (Trace::inst().on())
        Trace::inst().line("overrides.built_on_open", current_counts(), nullptr, true);
}

long long ms_since(std::chrono::steady_clock::time_point started)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
}

namespace
{
// The spans waiting for their first idle, and the one app-wide idle handler that settles them
std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> s_settling;
bool s_idle_bound = false;

void on_idle(wxIdleEvent &evt)
{
    evt.Skip();
    if (s_settling.empty())
        return;
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> settling;
    settling.swap(s_settling);
    for (const auto &[tag, started] : settling)
        measure(tag + ".settled", ms_since(started));
}
} // namespace

Span::Span(std::string tag) : m_tag(std::move(tag)), m_started(std::chrono::steady_clock::now())
{
    snapshot(m_tag + ".begin");
}

Span::~Span()
{
    measure(m_tag + ".end", ms_since(m_started));
    // The first idle after the handler: the layout and paint it caused have run
    if (!s_idle_bound && wxTheApp != nullptr)
    {
        wxTheApp->Bind(wxEVT_IDLE, &on_idle);
        s_idle_bound = true;
    }
    if (s_idle_bound)
        s_settling.emplace_back(m_tag, m_started);
}

#ifdef PREFLIGHT_TEST_HOOKS
namespace
{
// The period the probe's timer asks for while a window is open (Windows delivers it no faster than its timer tick)
constexpr int STALL_TICK_MS = 5;

// PREFLIGHT_UI_STALL_PROBE=0 turns the probe off, so an A/B can rule it out. Read once per session.
bool stall_probe_disabled()
{
    static const bool disabled = []()
    {
        const char *env = std::getenv("PREFLIGHT_UI_STALL_PROBE");
        return env != nullptr && std::string(env) == "0";
    }();
    return disabled;
}

// The UI thread's longest stay away from its event loop within a window: a timer on the UI thread ticks every
// STALL_TICK_MS while the window is open, and the longest gap between two ticks is the longest time the loop did not
// run. Windows delivers a timer tick only when no posted, input or paint message is waiting, so the ticks never hold
// a paint back. Everything runs on the UI thread. Leaked on purpose so nothing of it is destroyed after wxWidgets'
// teardown; stop() deletes the timer while wxWidgets is still up.
class StallProbe
{
public:
    static StallProbe &inst()
    {
        static StallProbe *probe = new StallProbe();
        return *probe;
    }

    // The timer exists only on the UI thread of a running application
    void begin()
    {
        if (m_stopped || wxTheApp == nullptr || !wxIsMainThread())
            return;
        if (!m_timer)
        {
            m_timer = std::make_unique<wxTimer>();
            m_timer->Bind(wxEVT_TIMER, [this](wxTimerEvent &) { on_tick(); });
        }
        m_open = true;
        m_max_ms = 0.0f;
        m_last_tick = std::chrono::steady_clock::now();
        m_timer->Start(STALL_TICK_MS);
    }

    float end()
    {
        if (!m_open)
            return 0.0f;
        m_open = false;
        m_timer->Stop();
        // The gap still running when the window closes counts as one
        return std::max(m_max_ms, ms_since_tick());
    }

    // At the application's teardown
    void stop()
    {
        m_stopped = true;
        m_open = false;
        m_timer.reset();
    }

private:
    void on_tick()
    {
        if (!m_open)
            return;
        const auto now = std::chrono::steady_clock::now();
        m_max_ms = std::max(m_max_ms, ms_between(m_last_tick, now));
        m_last_tick = now;
    }

    float ms_since_tick() const { return ms_between(m_last_tick, std::chrono::steady_clock::now()); }

    static float ms_between(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
    {
        return std::chrono::duration<float, std::milli>(to - from).count();
    }

    std::unique_ptr<wxTimer> m_timer;
    bool m_open{false};
    bool m_stopped{false};
    std::chrono::steady_clock::time_point m_last_tick;
    float m_max_ms{0.0f};
};

// Whether a window was ever opened, so the teardown does not create the probe (UI thread only)
bool s_stall_probe_used = false;
} // namespace

void stall_window_begin()
{
    if (stall_probe_disabled())
        return;
    s_stall_probe_used = true;
    StallProbe::inst().begin();
}

float stall_window_end()
{
    return s_stall_probe_used ? StallProbe::inst().end() : 0.0f;
}
#endif // PREFLIGHT_TEST_HOOKS

void finish()
{
#ifdef PREFLIGHT_TEST_HOOKS
    if (s_stall_probe_used)
        StallProbe::inst().stop();
#endif
    Trace::inst().finish();
}

#ifdef PREFLIGHT_TEST_HOOKS
void run_probe(const std::string &path,
               const std::function<wxStaticBoxSizer *(wxWindow *parent, const wxString &label)> &make_group)
{
#ifdef _WIN32
    constexpr int N = 100;
    constexpr int COLUMNS = 10;
    const int em = wxGetApp().em_unit();
    const wxSize cell(9 * em, 4 * em);
    const wxSize area(cell.x * COLUMNS, cell.y * (N / COLUMNS));

    // A visible window whose client area holds every instance, so each one paints
    auto *frame = new wxFrame(nullptr, wxID_ANY, "preFlight GUI probe", wxPoint(0, 0), wxDefaultSize,
                              wxCAPTION | wxFRAME_NO_TASKBAR);
    frame->SetClientSize(area);
    frame->Show();
    ::UpdateWindow((HWND) frame->GetHWND());

    struct Kind
    {
        const char *name;
        std::function<void(wxWindow *host, wxSizer *grid)> make;
    };
    const std::vector<Kind> kinds = {
        {"wxStaticText", [](wxWindow *host, wxSizer *grid)
         { grid->Add(new wxStaticText(host, wxID_ANY, "Probe label:"), 1, wxEXPAND); }},
        {"wxStaticBitmap", [](wxWindow *host, wxSizer *grid)
         { grid->Add(new wxStaticBitmap(host, wxID_ANY, *get_bmp_bundle("lock_closed")), 1, wxEXPAND); }},
        // A settings row's pin, lock and undo marks, painted by one window in place of three bitmaps
        {"RowIcons",
         [](wxWindow *host, wxSizer *grid)
         {
             auto *icons = new RowIcons(host, true, true, true);
             icons->SetIcon(RowIcons::Pin, *get_bmp_bundle("check_on", 16));
             icons->SetIcon(RowIcons::Lock, *get_bmp_bundle("lock_closed"));
             icons->SetIcon(RowIcons::Undo, *get_bmp_bundle("undo"));
             icons->SetTip(RowIcons::Lock, "Probe");
             grid->Add(icons, 1, wxEXPAND);
         }},
        // The layers of a TextInput, so its per-instance cost can be attributed
        {"wxTextCtrl",
         [em](wxWindow *host, wxSizer *grid)
         {
             grid->Add(new wxTextCtrl(host, wxID_ANY, "1.25", wxDefaultPosition, wxSize(7 * em, -1), wxBORDER_NONE), 1,
                       wxEXPAND);
         }},
        {"ThemedTextCtrl",
         [em](wxWindow *host, wxSizer *grid)
         {
             auto *text = new ThemedTextCtrl(host, wxID_ANY, "1.25", wxDefaultPosition, wxSize(7 * em, -1),
                                             wxBORDER_NONE);
             text->SetThemedColors(wxColour(0x30, 0x30, 0x30), *wxWHITE);
             grid->Add(text, 1, wxEXPAND);
         }},
        {"TextInput", [em](wxWindow *host, wxSizer *grid)
         { grid->Add(new ::TextInput(host, "1.25", "", "", wxDefaultPosition, wxSize(7 * em, -1)), 1, wxEXPAND); }},
        {"SpinInput",
         [em](wxWindow *host, wxSizer *grid)
         {
             grid->Add(new ::SpinInput(host, "5", "", wxDefaultPosition, wxSize(7 * em, -1), 0, 0, 100, 5), 1,
                       wxEXPAND);
         }},
        {"ComboBox",
         [em](wxWindow *host, wxSizer *grid)
         {
             auto *combo = new ::ComboBox(host, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(8 * em, -1), 0,
                                          nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
             combo->Append("Option");
             combo->SetSelection(0);
             grid->Add(combo, 1, wxEXPAND);
         }},
        {"CheckBox", [](wxWindow *host, wxSizer *grid) { grid->Add(new ::CheckBox(host), 1, wxEXPAND); }},
        // One icon's bitmap for a window, as every icon widget asks for it: shared by all its holders at
        // any display scale, so 0 GDI per instance
        {"SVG icon GetBitmapFor",
         [](wxWindow *host, wxSizer *grid)
         {
             struct Held : wxClientData
             {
                 wxBitmap bitmap;
             };
             auto *window = new wxWindow(host, wxID_ANY);
             auto *held = new Held;
             held->bitmap = get_bmp_bundle("check_on", 16)->GetBitmapFor(window);
             window->SetClientObject(held);
             grid->Add(window, 1, wxEXPAND);
         }},
        // The same for a bundle made from bitmaps (PNG icons, colour swatches)
        {"Bitmap bundle GetBitmapFor",
         [](wxWindow *host, wxSizer *grid)
         {
             struct Held : wxClientData
             {
                 wxBitmap bitmap;
             };
             auto *window = new wxWindow(host, wxID_ANY);
             auto *held = new Held;
             held->bitmap = get_solid_bmp_bundle(16, 16, "#3080C0")->GetBitmapFor(window);
             window->SetClientObject(held);
             grid->Add(window, 1, wxEXPAND);
         }},
        {"Button", [](wxWindow *host, wxSizer *grid) { grid->Add(new ::Button(host, "Probe"), 1, wxEXPAND); }},
        {"FlatStaticBox group",
         [&make_group](wxWindow *host, wxSizer *grid)
         {
             if (wxStaticBoxSizer *group = make_group(host, "Group"); group != nullptr)
                 grid->Add(group, 1, wxEXPAND);
         }},
        {"CollapsibleSection",
         [](wxWindow *host, wxSizer *grid)
         {
             // With its content container, as the sidebar builds one per category
             auto *section = new CollapsibleSection(host, "Section", true);
             section->SetContent(new wxPanel(section, wxID_ANY));
             grid->Add(section, 1, wxEXPAND);
         }},
    };

    boost::nowide::ofstream out(path);
    out << "widget\tn\tgdi_hidden_per\tuser_per\tgdi_painted_per\tuser_painted_per\tgdi_left_per\tuser_left_per\t"
           "focusable_per\n";
    auto per = [](long delta)
    {
        return wxString::Format("%.2f", double(delta) / N).ToStdString();
    };
    // The windows under a host that Tab stops on: a row's icons should be none
    std::function<long(const wxWindow *)> focusable = [&focusable](const wxWindow *window)
    {
        long count = 0;
        for (const wxWindow *child : window->GetChildren())
            count += (child->AcceptsFocusFromKeyboard() ? 1 : 0) + focusable(child);
        return count;
    };
    for (const Kind &kind : kinds)
    {
        const Counts before = current_counts();
        auto *host = new wxPanel(frame, wxID_ANY, wxPoint(0, 0), area);
        host->Hide();
        const Counts empty = current_counts();

        // Constructed and never shown
        auto *grid = new wxGridSizer(COLUMNS, 0, 0);
        for (int i = 0; i < N; ++i)
            kind.make(host, grid);
        host->SetSizer(grid);
        const Counts hidden = current_counts();

        // Laid out into the visible area and painted once, every child included
        host->Show();
        grid->SetDimension(wxPoint(0, 0), area);
        ::RedrawWindow((HWND) host->GetHWND(), nullptr, nullptr,
                       RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        const Counts painted = current_counts();
        const long focus_stops = focusable(host);

        // What destruction leaves behind: shared caches, or a leak when it grows with the count
        host->Destroy();
        const Counts left = current_counts();

        out << kind.name << '\t' << N << '\t' << per(hidden.gdi - empty.gdi) << '\t' << per(hidden.user - empty.user)
            << '\t' << per(painted.gdi - empty.gdi) << '\t' << per(painted.user - empty.user) << '\t'
            << per(left.gdi - before.gdi) << '\t' << per(left.user - before.user) << '\t' << per(focus_stops) << '\n';
    }
    frame->Destroy();
#else
    (void) path;
    (void) make_group;
#endif
}
#endif

} // namespace GuiBudget
} // namespace DSKY
