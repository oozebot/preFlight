///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <chrono>
#include <functional>
#include <string>

class wxWindow;
class wxString;
class wxStaticBoxSizer;

namespace DSKY
{

// The GUI's window and GDI budget. Windows allows a process 10,000 GDI objects and 10,000 USER
// objects; at either limit every new window, bitmap, brush or font fails.
//
// PREFLIGHT_GUI_BUDGET=<file> writes a trace: one tab-separated line per snapshot
// (t_ms, tag, gdi, user, delta_gdi, delta_user, value), and at exit the peaks a background sampler
// saw, the counts at exit and every registered debug counter. The GDI and USER columns are Windows
// only and read 0 elsewhere. Without the variable nothing is written; every snapshot still checks
// the soft ceiling.
namespace GuiBudget
{

// Either count at or above this bumps GUI_BUDGET_SOFT_CEILING once per crossing and logs both
// counts at error level. Nothing else changes.
constexpr int SOFT_CEILING = 9000;

// Scrolled content taller than this is past the 16-bit window coordinate range once scrolled;
// wx then moves windows by scrolling their parent, and layout handlers that move windows recurse.
constexpr int SCROLL_CONTENT_LIMIT = 32000;

bool tracing();

// One trace line: the current counts and their change since the previous line
void snapshot(const std::string &tag);

// A trace line that also carries a measured value (a content height, a duration in ms)
void measure(const std::string &tag, long long value);

// Milliseconds since `started`, on the steady clock every duration in the trace is measured with
long long ms_since(std::chrono::steady_clock::time_point started);

// A user action, timed: "<tag>.begin" when made, "<tag>.end" with the handler's ms when it goes out
// of scope, and "<tag>.settled" with the ms to the first idle after that (the layout and paint the
// handler caused: what the user waits for).
class Span
{
public:
    explicit Span(std::string tag);
    ~Span();
    Span(const Span &) = delete;
    Span &operator=(const Span &) = delete;

private:
    std::string m_tag;
    std::chrono::steady_clock::time_point m_started;
};

// A scrolled panel's content height after a layout that changed it. Traced under
// "<tag>.content_height" when the panel has a tag; crossing SCROLL_CONTENT_LIMIT bumps
// GUI_SCROLL_CONTENT_OVERFLOW once per crossing and logs at error level. `overflowing` is the
// panel's own crossing state.
void content_height(const std::string &tag, int height, bool &overflowing);

#ifdef PREFLIGHT_TEST_HOOKS
// A dialog that PREFLIGHT_AUTO_DISMISS_DIALOGS (a test hook) closed without showing it (a notice with
// its default button, a question declined: Cancel, else No): logged with its title and traced as
// "dialog.notice: <title>" or "dialog.question: <title>". A question (a dialog with Yes, No or
// Cancel) bumps GUI_QUESTION_DIALOG_AUTO_ANSWERED and logs at error level: the switch never agrees to
// anything, and an automated run is set up so that it never reaches a question.
void dialog_dismissed(const std::string &title, bool question);
#endif

// The override panel had to build its rows on a click (OVERRIDES_OPENED_BEFORE_PREBUILT): the
// design builds them before any click, at start-up and after a theme or scale change
void overrides_built_on_open();

// Writes the exit block and stops the sampler. Called once, from the application's teardown.
void finish();

#ifdef PREFLIGHT_TEST_HOOKS
// The UI thread's responsiveness over a window (a test hook build measures it for every slice, from its start to the
// first frame drawing its toolpaths): while the window is open a timer on the UI thread ticks every 5 ms (on Windows
// no faster than the system timer tick, 10 to 16 ms), and the longest gap between two ticks is how long the UI thread
// stayed away from its event loop. begin() opens (or reopens) the window; end() closes it and returns the longest gap
// in ms, the gap since the last tick included (0 when no window is open). Both on the UI thread.
// PREFLIGHT_UI_STALL_PROBE=0 makes begin() do nothing, so end() returns 0.
void stall_window_begin();
float stall_window_end();
#endif

#ifdef PREFLIGHT_TEST_HOOKS
// PREFLIGHT_GUI_PROBE=<file> (a test hook): builds 100 instances of every widget a settings row is
// made of in a hidden panel of a probe window, measures the GDI and USER cost per instance hidden,
// after one paint and after destruction, and writes one row per widget. `make_group` builds the
// sidebar's group box with its overlay header. Windows only; elsewhere it writes nothing.
void run_probe(const std::string &path,
               const std::function<wxStaticBoxSizer *(wxWindow *parent, const wxString &label)> &make_group);
#endif

} // namespace GuiBudget
} // namespace DSKY
