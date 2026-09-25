///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Roman Beránek @zavorka, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 ole00 @ole00
///|/ Copyright (c) 2021 Ilya @xorza
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "BackgroundSlicingProcess.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "luminary/config/thumbnails/Thumbnails.hpp"

#include <chrono>
#include <wx/app.h>
#include <wx/panel.h>
#include <wx/stdpaths.h>

// For zipped archive creation
#include <wx/stdstream.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <miniz.h>

// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "luminary/presets/app_config/AppConfig.hpp"
#include "luminary/layer/print/Print.hpp"
#include "luminary/platform/process/Process.hpp"
#include "luminary/gcode/interpret/GCodeObject.hpp"
#include "luminary/gcode/postprocess/PostProcessor.hpp"
#include "luminary/gcode/scripting/PreProcessor.hpp"
#include "luminary/platform/concurrency/Thread.hpp"
#include "luminary/core/Prelude.hpp"

#include <cassert>
#include <stdexcept>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#include <malloc.h>
#endif

#include <boost/format/format_fwd.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>
#include "I18N.hpp"
#include "RemovableDriveManager.hpp"

#include "DSKY/GUI/Plater.hpp"

namespace Luminary
{

bool SlicingProcessCompletedEvent::critical_error() const
{
    try
    {
        this->rethrow_exception();
    }
    catch (const Luminary::SlicingError &)
    {
        // Exception derived from SlicingError is non-critical.
        return false;
    }
    catch (...)
    {
    }
    return true;
}

bool SlicingProcessCompletedEvent::invalidate_plater() const
{
    if (critical_error())
    {
        try
        {
            this->rethrow_exception();
        }
        catch (const Luminary::ExportError &)
        {
            // Exception thrown by copying file does not ivalidate plater
            return false;
        }
        catch (...)
        {
        }
        return true;
    }
    return false;
}

std::pair<std::string, bool> SlicingProcessCompletedEvent::format_error_message() const
{
    std::string error;
    bool monospace = false;
    try
    {
        this->rethrow_exception();
    }
    catch (const std::bad_alloc &ex)
    {
        error =
            DSKY::format(_L("%s has encountered an error. It was likely caused by running out of memory. "
                            "If you are sure you have enough RAM on your system, this may also be a bug and we would "
                            "be glad if you reported it."),
                         PREFLIGHT_APP_NAME);
        error += "\n\n" + std::string(ex.what());
    }
    catch (const HardCrash &ex)
    {
        error = DSKY::format(_L("preFlight has encountered a fatal error: \"%1%\""), ex.what()) + "\n\n" +
                _u8L("Please save your project and restart preFlight. "
                     "We would be glad if you reported the issue.");
    }
    catch (PlaceholderParserError &ex)
    {
        error = ex.what();
        monospace = true;
    }
    catch (std::exception &ex)
    {
        error = ex.what();
    }
    catch (...)
    {
        error = "Unknown C++ exception.";
    }
    return std::make_pair(std::move(error), monospace);
}

void BackgroundSlicingProcess::set_temp_output_path(int bed_idx)
{
    // No longer needed with memory-based processing
    // boost::filesystem::path temp_path(wxStandardPaths::Get().GetTempDir().utf8_str().data());
    // temp_path /= (boost::format(".%1%_%2%.gcode") % get_current_pid() % bed_idx).str();
    // m_temp_output_path = temp_path.string();
}

BackgroundSlicingProcess::~BackgroundSlicingProcess()
{
    this->stop();
    this->join_background_thread();

    // No temp files to clean up with memory-based processing
}

bool BackgroundSlicingProcess::select_technology(PrinterTechnology tech)
{
    bool changed = false;
    if (m_print == nullptr || m_print->technology() != tech)
    {
        if (m_print != nullptr)
            this->reset();
        switch (tech)
        {
        case ptFFF:
            m_print = m_fff_print;
            break;
        default:
            assert(false);
            break;
        }
        changed = true;
    }
    if (tech == ptFFF)
        m_print = m_fff_print;
    assert(m_print != nullptr);
    return changed;
}

PrinterTechnology BackgroundSlicingProcess::current_printer_technology() const
{
    return m_print->technology();
}

std::string BackgroundSlicingProcess::output_filepath_for_project(const boost::filesystem::path &project_path)
{
    assert(m_print != nullptr);
    if (project_path.empty())
        return m_print->output_filepath("");
    return m_print->output_filepath(project_path.parent_path().string(), project_path.stem().string());
}

// This function may one day be merged into the Print, but historically the print was separated
// from the G-code generator.
void BackgroundSlicingProcess::process_fff()
{
    assert(m_print == m_fff_print);
    {
        const AppConfig *app_config = DSKY::wxGetApp().app_config;
        size_t threshold = 10'000'000;
        if (app_config)
        {
            std::string val = app_config->get("preview_detail");
            if (!val.empty())
            {
                try
                {
                    threshold = std::stoull(val);
                }
                catch (...)
                {
                    threshold = 10'000'000;
                }
            }
        }
        m_fff_print->set_preview_detail_threshold(threshold);
    }
    m_print->process();
    m_slicing_event_poster->postSlicingCompleted(
        (int) (m_fff_print->step_state_with_timestamp(PrintStep::psSlicingFinished).timestamp));
    m_fff_print->export_gcode("", m_gcode_result,
                              [this](const ThumbnailsParams &params) { return this->render_thumbnails(params); });
    if (this->set_step_started(bspsGCodeFinalize))
    {
        if (!m_export_path.empty())
        {
            m_slicing_event_poster->postExportBegan();
            finalize_gcode(m_export_path, m_export_path_on_removable_media);
        }

        // Note: Don't set 100% here - data conversion (85-100%) happens during preview reload
        this->set_step_done(bspsGCodeFinalize);
    }
}

void BackgroundSlicingProcess::thread_proc()
{
    set_current_thread_name("pf_BgSlcPcs");
    name_tbb_thread_pool_threads_set_locale();

    // Set "C" locales and enforce OSX QoS level on all threads entering an arena.
    // The cost of the callback is quite low: The callback is called once per thread
    // entering a parallel loop and the callback is guarded with a thread local
    // variable to be executed just once.
    TBBLocalesSetter setter;

    assert(m_print != nullptr);
    assert(m_print == m_fff_print);
    std::unique_lock<std::mutex> lck(m_mutex);
    // Let the caller know we are ready to run the background processing task.
    m_state = STATE_IDLE;
    lck.unlock();
    m_condition.notify_one();
    for (;;)
    {
        assert(m_state == STATE_IDLE || m_state == STATE_CANCELED || m_state == STATE_FINISHED);
        // Wait until a new task is ready to be executed, or this thread should be finished.
        lck.lock();
        m_condition.wait(lck, [this]() { return m_state == STATE_STARTED || m_state == STATE_EXIT; });
        if (m_state == STATE_EXIT)
            // Exiting this thread.
            break;
        // Process the background slicing task.
        m_state = STATE_RUNNING;
        lck.unlock();
        std::exception_ptr exception;
#ifdef _WIN32
        this->call_process_seh_throw(exception);
#else
        this->call_process(exception);
#endif
        m_print->finalize();
        lck.lock();
        m_state = m_print->canceled() ? STATE_CANCELED : STATE_FINISHED;
        if (m_print->cancel_status() != Print::CANCELED_INTERNAL)
        {
            // Only post the canceled event, if canceled by user.
            // Don't post the canceled event, if canceled from Print::apply().
            m_slicing_event_poster->postProcessCompleted((m_state == STATE_CANCELED)
                                                             ? DSKY::SlicingCompletedStatus::Cancelled
                                                         : exception ? DSKY::SlicingCompletedStatus::Error
                                                                     : DSKY::SlicingCompletedStatus::Finished,
                                                         exception);
            // Cancelled by the user, not internally, thus cleanup() was not called yet.
            // Otherwise cleanup() is called from Print::apply()
            m_print->cleanup();
        }
        m_print->restart();
        lck.unlock();
        // Let the UI thread wake up if it is waiting for the background task to finish.
        m_condition.notify_one();
        // Let the UI thread see the result.
    }
    m_state = STATE_EXITED;
    lck.unlock();
    // End of the background processing thread. The UI thread should join m_thread now.
}

#ifdef _WIN32
// Only these SEH exceptions will be catched and turned into Luminary::HardCrash C++ exceptions.
static bool is_win32_seh_harware_exception(unsigned long ex) throw()
{
    return ex == STATUS_ACCESS_VIOLATION || ex == STATUS_DATATYPE_MISALIGNMENT || ex == STATUS_FLOAT_DIVIDE_BY_ZERO ||
           ex == STATUS_FLOAT_OVERFLOW || ex == STATUS_FLOAT_UNDERFLOW ||
#ifdef STATUS_FLOATING_RESEVERED_OPERAND
           ex == STATUS_FLOATING_RESEVERED_OPERAND ||
#endif // STATUS_FLOATING_RESEVERED_OPERAND
           ex == STATUS_ILLEGAL_INSTRUCTION || ex == STATUS_PRIVILEGED_INSTRUCTION ||
           ex == STATUS_INTEGER_DIVIDE_BY_ZERO || ex == STATUS_INTEGER_OVERFLOW || ex == STATUS_STACK_OVERFLOW;
}

// Rethrow some SEH exceptions as Luminary::HardCrash C++ exceptions.
static void rethrow_seh_exception(unsigned long win32_seh_catched)
{
    if (win32_seh_catched)
    {
        // Rethrow SEH exception as Slicer::HardCrash.
        if (win32_seh_catched == STATUS_ACCESS_VIOLATION || win32_seh_catched == STATUS_DATATYPE_MISALIGNMENT)
            throw Luminary::HardCrash(_u8L("Access violation"));
        if (win32_seh_catched == STATUS_ILLEGAL_INSTRUCTION || win32_seh_catched == STATUS_PRIVILEGED_INSTRUCTION)
            throw Luminary::HardCrash(_u8L("Illegal instruction"));
        if (win32_seh_catched == STATUS_FLOAT_DIVIDE_BY_ZERO || win32_seh_catched == STATUS_INTEGER_DIVIDE_BY_ZERO)
            throw Luminary::HardCrash(_u8L("Divide by zero"));
        if (win32_seh_catched == STATUS_FLOAT_OVERFLOW || win32_seh_catched == STATUS_INTEGER_OVERFLOW)
            throw Luminary::HardCrash(_u8L("Overflow"));
        if (win32_seh_catched == STATUS_FLOAT_UNDERFLOW)
            throw Luminary::HardCrash(_u8L("Underflow"));
#ifdef STATUS_FLOATING_RESEVERED_OPERAND
        if (win32_seh_catched == STATUS_FLOATING_RESEVERED_OPERAND)
            throw Luminary::HardCrash(_u8L("Floating reserved operand"));
#endif // STATUS_FLOATING_RESEVERED_OPERAND
        if (win32_seh_catched == STATUS_STACK_OVERFLOW)
            throw Luminary::HardCrash(_u8L("Stack overflow"));
    }
}

// Wrapper for Win32 structured exceptions. Win32 structured exception blocks and C++ exception blocks cannot be mixed in the same function.
unsigned long BackgroundSlicingProcess::call_process_seh(std::exception_ptr &ex) throw()
{
    unsigned long win32_seh_catched = 0;
    __try
    {
        this->call_process(ex);
    }
    __except (is_win32_seh_harware_exception(GetExceptionCode()))
    {
        win32_seh_catched = GetExceptionCode();
    }
    return win32_seh_catched;
}
void BackgroundSlicingProcess::call_process_seh_throw(std::exception_ptr &ex) throw()
{
    unsigned long win32_seh_catched = this->call_process_seh(ex);
    if (win32_seh_catched)
    {
        // Rethrow SEH exception as Slicer::HardCrash.
        try
        {
            rethrow_seh_exception(win32_seh_catched);
        }
        catch (...)
        {
            ex = std::current_exception();
        }
    }
}
#endif // _WIN32

void BackgroundSlicingProcess::call_process(std::exception_ptr &ex) throw()
{
    try
    {
        assert(m_print != nullptr);
        switch (m_print->technology())
        {
        case ptFFF:
            this->process_fff();
            break;
        default:
            m_print->process();
            break;
        }
    }
    catch (CanceledException & /* ex */)
    {
        // Canceled, this is all right.
        assert(m_print->canceled());
        ex = std::current_exception();
    }
    catch (...)
    {
        ex = std::current_exception();
    }
}

#ifdef _WIN32
unsigned long BackgroundSlicingProcess::thread_proc_safe_seh() throw()
{
    unsigned long win32_seh_catched = 0;
    __try
    {
        this->thread_proc_safe();
    }
    __except (is_win32_seh_harware_exception(GetExceptionCode()))
    {
        win32_seh_catched = GetExceptionCode();
    }
    return win32_seh_catched;
}
void BackgroundSlicingProcess::thread_proc_safe_seh_throw() throw()
{
    unsigned long win32_seh_catched = this->thread_proc_safe_seh();
    if (win32_seh_catched)
    {
        // Rethrow SEH exception as Slicer::HardCrash.
        try
        {
            rethrow_seh_exception(win32_seh_catched);
        }
        catch (...)
        {
            wxTheApp->OnUnhandledException();
        }
    }
}
#endif // _WIN32

void BackgroundSlicingProcess::thread_proc_safe() throw()
{
    try
    {
        this->thread_proc();
    }
    catch (...)
    {
        wxTheApp->OnUnhandledException();
    }
}

void BackgroundSlicingProcess::join_background_thread()
{
    std::unique_lock<std::mutex> lck(m_mutex);
    if (m_state == STATE_INITIAL)
    {
        // Worker thread has not been started yet.
        assert(!m_thread.joinable());
    }
    else
    {
        assert(m_state == STATE_IDLE);
        assert(m_thread.joinable());
        // Notify the worker thread to exit.
        m_state = STATE_EXIT;
        lck.unlock();
        m_condition.notify_one();
        // Wait until the worker thread exits.
        m_thread.join();
    }
}

bool BackgroundSlicingProcess::start()
{
    if (m_print->empty())
        // The print is empty (no object in Model, or all objects are out of the print bed).
        return false;

    std::unique_lock<std::mutex> lck(m_mutex);
    if (m_state == STATE_INITIAL)
    {
        // The worker thread is not running yet. Start it.
        assert(!m_thread.joinable());
        m_thread = create_thread(
            [this]
            {
#ifdef _WIN32
                this->thread_proc_safe_seh_throw();
#else  // _WIN32
                this->thread_proc_safe();
#endif // _WIN32
            });
        // Wait until the worker thread is ready to execute the background processing task.
        m_condition.wait(lck, [this]() { return m_state == STATE_IDLE; });
    }
    assert(m_state == STATE_IDLE || this->running());
    if (this->running())
        // The background processing thread is already running.
        return false;
    if (!this->idle())
        throw Luminary::RuntimeError("Cannot start a background task, the worker thread is not idle.");
    m_state = STATE_STARTED;
    m_print->set_cancel_callback([this]() { this->stop_internal(); });
    lck.unlock();
    m_condition.notify_one();
    return true;
}

// To be called on the UI thread.
bool BackgroundSlicingProcess::stop()
{
    // m_print->state_mutex() shall NOT be held. Unfortunately there is no interface to test for it.
    std::unique_lock<std::mutex> lck(m_mutex);
    if (m_state == STATE_INITIAL)
    {
        //		m_export_path.clear();
        return false;
    }
    //	assert(this->running());
    if (m_state == STATE_STARTED || m_state == STATE_RUNNING)
    {
        // Cancel any task planned by the background thread on UI thread.
        cancel_ui_task(m_ui_task);
        m_print->cancel();
        // Wait until the background processing stops by being canceled.
        m_condition.wait(lck, [this]() { return m_state == STATE_CANCELED; });
        // In the "Canceled" state. Reset the state to "Idle".
        m_state = STATE_IDLE;
        m_print->set_cancel_callback([]() {});
    }
    else if (m_state == STATE_FINISHED || m_state == STATE_CANCELED)
    {
        // In the "Finished" or "Canceled" state. Reset the state to "Idle".
        m_state = STATE_IDLE;
        m_print->set_cancel_callback([]() {});
    }
    //	m_export_path.clear();
    return true;
}

bool BackgroundSlicingProcess::reset()
{
    bool stopped = this->stop();
    this->reset_export();
    m_print->clear();
    this->invalidate_all_steps();
    return stopped;
}

// To be called by Print::apply() on the UI thread through the Print::m_cancel_callback to stop the background
// processing before changing any data of running or finalized milestones.
// This function shall not trigger any UI update through the wxWidgets event.
void BackgroundSlicingProcess::stop_internal()
{
    // m_print->state_mutex() shall be held. Unfortunately there is no interface to test for it.
    if (m_state == STATE_IDLE)
        // The worker thread is waiting on m_mutex/m_condition for wake up. The following lock of the mutex would block.
        return;
    std::unique_lock<std::mutex> lck(m_mutex);
    assert(m_state == STATE_STARTED || m_state == STATE_RUNNING || m_state == STATE_FINISHED ||
           m_state == STATE_CANCELED);
    if (m_state == STATE_STARTED || m_state == STATE_RUNNING)
    {
        // Cancel any task planned by the background thread on UI thread.
        cancel_ui_task(m_ui_task);
        // At this point of time the worker thread may be blocking on m_print->state_mutex().
        // Set the print state to canceled before unlocking the state_mutex(), so when the worker thread wakes up,
        // it throws the CanceledException().
        m_print->cancel_internal();
        // Allow the worker thread to wake up if blocking on a milestone.
        m_print->state_mutex().unlock();
        // Wait until the background processing stops by being canceled.
        m_condition.wait(lck, [this]() { return m_state == STATE_CANCELED; });
        // Lock it back to be in a consistent state.
        m_print->state_mutex().lock();
    }
    // In the "Canceled" state. Reset the state to "Idle".
    m_state = STATE_IDLE;
    m_print->set_cancel_callback([]() {});
}

// Execute task from background thread on the UI thread. Returns true if processed, false if cancelled.
bool BackgroundSlicingProcess::execute_ui_task(std::function<void()> task)
{
    bool running = false;
    if (m_mutex.try_lock())
    {
        // Cancellation is either not in process, or already canceled and waiting for us to finish.
        // There must be no UI task planned.
        assert(!m_ui_task);
        if (!m_print->canceled())
        {
            running = true;
            m_ui_task = std::make_shared<UITask>();
        }
        m_mutex.unlock();
    }
    else
    {
        // Cancellation is in process.
    }

    bool result = false;
    if (running)
    {
        std::shared_ptr<UITask> ctx = m_ui_task;
        m_slicing_event_poster->callOnUIThreadAsync(
            [task, ctx]()
            {
                assert(ctx->state == UITask::Planned || ctx->state == UITask::Canceled);
                if (ctx->state == UITask::Planned)
                {
                    task();
                    std::unique_lock<std::mutex> lck(ctx->mutex);
                    ctx->state = UITask::Finished;
                }
                ctx->condition.notify_all();
            });

        {
            std::unique_lock<std::mutex> lock(ctx->mutex);
            ctx->condition.wait(lock,
                                [&ctx] { return ctx->state == UITask::Finished || ctx->state == UITask::Canceled; });
        }
        result = ctx->state == UITask::Finished;
        m_ui_task.reset();
    }

    return result;
}

// To be called on the UI thread from ::stop() and ::stop_internal().
void BackgroundSlicingProcess::cancel_ui_task(std::shared_ptr<UITask> task)
{
    if (task)
    {
        std::unique_lock<std::mutex> lck(task->mutex);
        task->state = UITask::Canceled;
        lck.unlock();
        task->condition.notify_all();
    }
}

bool BackgroundSlicingProcess::empty() const
{
    assert(m_print != nullptr);
    return m_print->empty();
}

std::string BackgroundSlicingProcess::validate(std::vector<std::string> *warnings)
{
    assert(m_print != nullptr);
    return m_print->validate(warnings);
}

// Apply config over the print. Returns false, if the new config values caused any of the already
// processed steps to be invalidated, therefore the task will need to be restarted.
Print::ApplyStatus BackgroundSlicingProcess::apply(const Model &model, const DynamicPrintConfig &config,
                                                   std::vector<std::string> *warnings, bool release_memory)
{
    assert(m_print != nullptr);
    assert(config.opt_enum<PrinterTechnology>("printer_technology") == m_print->technology());
#ifdef PREFLIGHT_PYTHON_PREPROCESSOR
    // Read preprocessing config from AppConfig on the UI thread (thread-safe)
    if (m_print->technology() == ptFFF)
    {
        auto *app_config = DSKY::wxGetApp().app_config;
        m_fff_print->set_preprocessing_consent(app_config->get_bool("preprocessing_consent_accepted"));
        m_fff_print->set_preprocessing_category_order(app_config->get("preprocessing_category_order"));
        // Wall-clock limit per script call in minutes, 0 for none; 5 when the key is absent.
        double timeout_minutes = 5.0;
        if (const std::string v = app_config->get("preprocessing_timeout_minutes"); !v.empty())
        {
            try
            {
                timeout_minutes = std::stod(v);
            }
            catch (...)
            {
            }
        }
        m_fff_print->set_preprocessing_timeout_seconds(std::max(0.0, timeout_minutes) * 60.0);

        // Pass the project directory so preprocessing can resolve relative script paths
        wxString proj = DSKY::wxGetApp().plater()->get_project_filename();
        if (!proj.empty())
        {
            boost::filesystem::path proj_path(DSKY::into_u8(proj));
            m_fff_print->set_project_dir(proj_path.parent_path().string());
        }
        else
            m_fff_print->set_project_dir(std::string());
    }
#endif
    Print::ApplyStatus invalidated = m_print->apply(model, config, warnings);
    if ((invalidated & PrintBase::APPLY_STATUS_INVALIDATED) != 0 && m_print->technology() == ptFFF &&
        !m_fff_print->is_step_done(psGCodeExport))
    {
        // Some FFF status was invalidated, and the G-code was not exported yet.
        // Let the G-code preview UI know that the final G-code preview is not valid.
        // In addition, this early memory deallocation reduces memory footprint.
        if (m_gcode_result != nullptr)
        {
            m_gcode_result->reset();
#ifdef _WIN32
            if (release_memory)
            {
                _heapmin(); // Compact the heap
                // Force working set reduction
                SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T) -1, (SIZE_T) -1);
            }
#endif
        }
    }
    return invalidated;
}

void BackgroundSlicingProcess::set_task(const PrintBase::TaskParams &params)
{
    assert(m_print != nullptr);
    m_print->set_task(params);
}

// Set the output path of the G-code.
void BackgroundSlicingProcess::schedule_export(const std::string &path, bool export_path_on_removable_media)
{
    assert(m_export_path.empty());
    if (!m_export_path.empty())
        return;

    // Guard against entering the export step before changing the export path.
    std::scoped_lock<std::mutex> lock(m_print->state_mutex());
    this->invalidate_step(bspsGCodeFinalize);
    m_export_path = path;
    m_export_path_on_removable_media = export_path_on_removable_media;
}

void BackgroundSlicingProcess::reset_export()
{
    assert(!this->running());
    if (!this->running())
    {
        m_export_path.clear();
        m_export_path_on_removable_media = false;
        // invalidate_step expects the mutex to be locked.
        std::scoped_lock<std::mutex> lock(m_print->state_mutex());
        this->invalidate_step(bspsGCodeFinalize);
    }
}

bool BackgroundSlicingProcess::set_step_started(BackgroundSlicingProcessStep step)
{
    return m_step_state.set_started(step, m_print->state_mutex(), [this]() { this->throw_if_canceled(); });
}

void BackgroundSlicingProcess::set_step_done(BackgroundSlicingProcessStep step)
{
    m_step_state.set_done(step, m_print->state_mutex(), [this]() { this->throw_if_canceled(); });
}

bool BackgroundSlicingProcess::is_step_done(BackgroundSlicingProcessStep step) const
{
    return m_step_state.is_done(step, m_print->state_mutex());
}

bool BackgroundSlicingProcess::invalidate_step(BackgroundSlicingProcessStep step)
{
    bool invalidated = m_step_state.invalidate(step, [this]() { this->stop_internal(); });
    return invalidated;
}

bool BackgroundSlicingProcess::invalidate_all_steps()
{
    return m_step_state.invalidate_all([this]() { this->stop_internal(); });
}

void BackgroundSlicingProcess::direct_export_gcode(const std::string &path, bool path_on_removable_media)
{
    // Perform the final post-processing of the export path by applying the print statistics over the file name.
    std::string export_path = m_fff_print->print_statistics().finalize_output_path(path);

    if (!m_gcode_result || !m_gcode_result->gcode_object)
        throw Luminary::ExportError("No G-code available for export");

    // Track temp file for cleanup on failure
    std::string text_path;

    try
    {
        // Write text gcode to disk first (for both binary and text modes).
        // Post-processing scripts need text to work on before binarization.
        const std::string &buf = m_gcode_result->gcode_object->text_buffer();
        text_path = export_path;
        if (m_gcode_result->is_binary_file && m_gcode_result->binary_data.has_value())
            text_path = export_path + ".tmp";

        FILE *file = boost::nowide::fopen(text_path.c_str(), "wb");
        if (!file)
            throw Luminary::ExportError(DSKY::format(_L("Failed to open G-code file for writing: %1%"), text_path));

        size_t written = fwrite(buf.c_str(), 1, buf.size(), file);
        fclose(file);

        if (written != buf.size())
        {
            boost::filesystem::remove(text_path);
            throw Luminary::ExportError(DSKY::format(_L("Failed to write complete G-code to file: %1%"), text_path));
        }

        // Run post-processing scripts on the text file
        run_post_process_scripts(text_path, m_fff_print->full_print_config());

        // For binary mode: binarize the (possibly script-modified) text file
        if (m_gcode_result->is_binary_file && m_gcode_result->binary_data.has_value())
        {
            GCodeProcessor::write_binary_gcode_from_file(export_path, text_path, *m_gcode_result->binary_data);
            boost::filesystem::remove(text_path);
        }
    }
    catch (...)
    {
        if (text_path != export_path && !text_path.empty())
            boost::filesystem::remove(text_path);
        throw Luminary::ExportError(DSKY::format(_L("Error exporting G-code: %1%"), export_path));
    }

    m_print->set_status(100, DSKY::format(_L("G-code file exported to %1%"), export_path));
}

// Prepare upload directly from in-memory G-code - no re-slicing needed.
// Writes the virtual G-code buffer to a temp file, runs post-processing scripts,
// and sets the source path on the upload job so it's ready for enqueue().
void BackgroundSlicingProcess::direct_prepare_upload(Luminary::PrintHostJob &upload_job)
{
    if (!m_gcode_result || !m_gcode_result->gcode_object)
        throw Luminary::ExportError("No G-code available for upload. Please slice the model first.");

    // Generate a unique temp path for the upload source file
    boost::filesystem::path source_path = boost::filesystem::temp_directory_path() /
                                          boost::filesystem::unique_path("." PREFLIGHT_APP_KEY
                                                                         ".upload.%%%%-%%%%-%%%%-%%%%");

    std::string text_path;

    try
    {
        // Write text gcode to a temp file first so post-processing scripts can run on it
        const std::string &buf = m_gcode_result->gcode_object->text_buffer();
        text_path = source_path.string();
        if (m_gcode_result->is_binary_file && m_gcode_result->binary_data.has_value())
            text_path = source_path.string() + ".tmp";

        FILE *file = boost::nowide::fopen(text_path.c_str(), "wb");
        if (!file)
            throw Luminary::ExportError("Failed to open temporary file for upload");

        size_t written = fwrite(buf.c_str(), 1, buf.size(), file);
        fclose(file);

        if (written != buf.size())
        {
            boost::filesystem::remove(text_path);
            throw Luminary::ExportError("Failed to write G-code to temporary file for upload");
        }

        // Finalize the upload filename with print statistics
        upload_job.upload_data.upload_path = m_fff_print->print_statistics().finalize_output_path(
            upload_job.upload_data.upload_path.string());

        // Run post-processing scripts on the text file
        std::string source_path_str = text_path;
        std::string output_name_str = upload_job.upload_data.upload_path.string();
        if (run_post_process_scripts(source_path_str, false, upload_job.printhost->get_name(), output_name_str,
                                     m_fff_print->full_print_config()))
            upload_job.upload_data.upload_path = output_name_str;

        // For binary mode: binarize the (possibly script-modified) text file
        if (m_gcode_result->is_binary_file && m_gcode_result->binary_data.has_value())
        {
            GCodeProcessor::write_binary_gcode_from_file(source_path.string(), text_path, *m_gcode_result->binary_data);
            boost::filesystem::remove(text_path);
        }
    }
    catch (...)
    {
        if (text_path != source_path.string() && !text_path.empty())
            boost::filesystem::remove(text_path);
        boost::filesystem::remove(source_path);
        throw;
    }

    upload_job.upload_data.source_path = std::move(source_path);
}

// A copy of the sliced G-code text for Export to Script. The export runs on the GUI job worker, so it
// works on its own copy rather than on a buffer a re-slice could replace meanwhile.
std::string BackgroundSlicingProcess::copy_gcode_text() const
{
    if (m_gcode_result == nullptr || m_gcode_result->gcode_object == nullptr)
        throw Luminary::ExportError("No G-code available for export");
    return m_gcode_result->gcode_object->text_buffer();
}

double BackgroundSlicingProcess::preprocessing_timeout_seconds() const
{
    return m_fff_print != nullptr ? m_fff_print->preprocessing_timeout_seconds() : 0.0;
}

// Write the final G-code to target location (possibly a SD card).
void BackgroundSlicingProcess::finalize_gcode(const std::string &path, const bool path_on_removable_media)
{
    m_print->set_status(95, _u8L("Running post-processing scripts"));

    // Perform the final post-processing of the export path by applying the print statistics over the file name.
    std::string export_path = m_fff_print->print_statistics().finalize_output_path(path);

    if (!m_gcode_result || !m_gcode_result->gcode_object)
        throw Luminary::ExportError("No G-code available for export");

    std::string text_path;

    try
    {
        // Write text gcode to disk first (scripts need text to work on before binarization)
        const std::string &buf = m_gcode_result->gcode_object->text_buffer();
        text_path = export_path;
        if (m_gcode_result->is_binary_file && m_gcode_result->binary_data.has_value())
            text_path = export_path + ".tmp";

        FILE *file = boost::nowide::fopen(text_path.c_str(), "wb");
        if (!file)
            throw Luminary::ExportError(DSKY::format(_L("Failed to open G-code file for writing: %1%"), text_path));

        size_t written = fwrite(buf.c_str(), 1, buf.size(), file);
        fclose(file);

        if (written != buf.size())
        {
            boost::filesystem::remove(text_path);
            throw Luminary::ExportError(DSKY::format(_L("Failed to write G-code to file: %1%"), text_path));
        }

        run_post_process_scripts(text_path, m_fff_print->full_print_config());

        // For binary mode: binarize the (possibly script-modified) text file
        if (m_gcode_result->is_binary_file && m_gcode_result->binary_data.has_value())
        {
            GCodeProcessor::write_binary_gcode_from_file(export_path, text_path, *m_gcode_result->binary_data);
            boost::filesystem::remove(text_path);
        }
    }
    catch (...)
    {
        if (text_path != export_path && !text_path.empty())
            boost::filesystem::remove(text_path);
        throw Luminary::ExportError(DSKY::format(_L("Error exporting G-code: %1%"), export_path));
    }

    m_print->set_status(100, DSKY::format(_L("G-code file exported to %1%"), export_path));
}

// Executed by the background thread, to start a task on the UI thread.
ThumbnailsList BackgroundSlicingProcess::render_thumbnails(const ThumbnailsParams &params)
{
    ThumbnailsList thumbnails;
    if (m_thumbnail_cb)
        this->execute_ui_task([this, &params, &thumbnails]() { thumbnails = m_thumbnail_cb(params); });
    return thumbnails;
}

}; // namespace Luminary
