///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "SeqArrangeJob.hpp"

#include "luminary/arrange/helper/ArrangeHelper.hpp"

#include "DSKY/GUI/GLCanvas3D.hpp"
#include "DSKY/GUI/GUI_App.hpp"
#include "DSKY/GUI/GUI_ObjectList.hpp"
#include "DSKY/GUI/I18N.hpp"
#include "DSKY/GUI/Plater.hpp"
#include "DSKY/GUI/MsgDialog.hpp"
#include "DSKY/GUI/format.hpp"

namespace DSKY
{
using namespace Luminary;

SeqArrangeJob::SeqArrangeJob(const Model &model, const DynamicPrintConfig &config, bool current_bed_only)
{
    m_seq_arrange.reset(new SeqArrange(model, config, current_bed_only));
}

void SeqArrangeJob::process(Ctl &ctl)
{
    class SeqArrangeJobException : std::exception
    {
    };

    try
    {
        m_seq_arrange->process_seq_arrange(
            [&](int progress)
            {
                ctl.update_status(progress, _u8L("Arranging for sequential print"));
                if (ctl.was_canceled())
                    throw SeqArrangeJobException();
            });
    }
    catch (const SeqArrangeJobException &)
    {
        ctl.update_status(100, ""); // Hide progress notification.
    }
    catch (const std::exception &)
    {
        ctl.update_status(100, ""); // Hide progress notification.
        throw;
    }
}

void SeqArrangeJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    // If the task was cancelled, the stopping exception was already caught
    // in 'process' function. Any other exception propagates through here.
    bool error = false;
    if (eptr)
    {
        try
        {
            std::rethrow_exception(eptr);
        }
        catch (const ExceptionCannotApplySeqArrange &)
        {
            ErrorDialog dlg(wxGetApp().plater(),
                            _L("The result of the single-bed arrange would scatter "
                               "instances of a single object between several beds, possibly affecting order of printing "
                               "of the non-selected beds. Consider using global arrange across all beds."),
                            false);
            dlg.ShowModal();
            error = true;
            eptr = nullptr; // The exception is handled.
        }
        catch (const Sequential::ObjectTooLargeException &)
        {
            ErrorDialog dlg(wxGetApp().plater(), _L("One of the objects is too large to fit the bed."), false);
            dlg.ShowModal();
            error = true;
            eptr = nullptr; // The exception is handled.
        }
        catch (const std::runtime_error &ex)
        {
            ErrorDialog dlg(wxGetApp().plater(), DSKY::format_wxstr(_L("Internal error: %1%"), ex.what()), false);
            dlg.ShowModal();
            error = true;
            eptr = nullptr; // The exception is handled.
        }
    }

    if (!canceled && !error)
    {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Arrange for sequential print"));
        m_seq_arrange->apply_seq_arrange(wxGetApp().model());
        wxGetApp().plater()->canvas3D()->reload_scene(true, true);
        wxGetApp().obj_list()->update_after_undo_redo();
    }
    m_seq_arrange.reset();
}

} // namespace DSKY
