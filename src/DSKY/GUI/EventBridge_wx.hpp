///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "EventBridge.hpp"

#include <wx/event.h>

namespace DSKY
{
using namespace Luminary;

// Maps CanvasEventType enum to the corresponding wxEventType constant.
wxEventType canvas_event_to_wx(CanvasEventType type);

// wx implementation of ICanvasEventPoster.
// Posts wxEvents to a target wxEvtHandler, preserving the existing wx event dispatch.
class CanvasEventPoster_wx : public ICanvasEventPoster
{
public:
    explicit CanvasEventPoster_wx(wxEvtHandler *target) : m_target(target) {}

    void postEvent(CanvasEventType type) override;
    void postEvent(CanvasEventType type, int data) override;
    void postEvent(CanvasEventType type, bool data) override;
    void postEvent(CanvasEventType type, float data) override;
    void postEvent(CanvasEventType type, const std::pair<Vec2d, bool> &data) override;
    void postEvent(CanvasEventType type, const std::array<Vec3d, 2> &data) override;
    void postEvent(CanvasEventType type, const HeightProfileSmoothingParams &data) override;
    void postEvent(CanvasEventType type, const std::string &data) override;
    void postKeyEvent(CanvasEventType type, int key_code, int modifiers) override;

private:
    wxEvtHandler *m_target;
};

// wx implementation of ISlicingEventPoster.
// Posts wxEvents to a target wxEvtHandler from the background slicing thread.
class SlicingEventPoster_wx : public ISlicingEventPoster
{
public:
    SlicingEventPoster_wx(wxEvtHandler *target, int slicing_update_id, int slicing_completed_id, int finished_id,
                          int export_began_id)
        : m_target(target)
        , m_slicing_update_id(slicing_update_id)
        , m_slicing_completed_id(slicing_completed_id)
        , m_finished_id(finished_id)
        , m_export_began_id(export_began_id)
    {
    }

    void postSlicingStatus(const PrintBase::SlicingStatus &status) override;
    void postSlicingCompleted(int timestamp) override;
    void postExportBegan() override;
    void postProcessCompleted(SlicingCompletedStatus status, std::exception_ptr exception) override;
    void callOnUIThreadAsync(std::function<void()> task) override;

private:
    wxEvtHandler *m_target;
    int m_slicing_update_id;
    int m_slicing_completed_id;
    int m_finished_id;
    int m_export_began_id;
};

} // namespace DSKY

