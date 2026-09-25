///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "EventBridge_wx.hpp"
#include "Event.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "GLCanvas3D.hpp"
#include "GLToolbar.hpp"

#include <wx/app.h>

namespace DSKY
{
using namespace Luminary;

wxEventType canvas_event_to_wx(CanvasEventType type)
{
    switch (type)
    {
    case CanvasEventType::ObjectSelect:
        return EVT_GLCANVAS_OBJECT_SELECT;
    case CanvasEventType::RemoveObject:
        return EVT_GLCANVAS_REMOVE_OBJECT;
    case CanvasEventType::SelectAll:
        return EVT_GLCANVAS_SELECT_ALL;
    case CanvasEventType::ReloadFromDisk:
        return EVT_GLCANVAS_RELOAD_FROM_DISK;
    case CanvasEventType::Arrange:
        return EVT_GLCANVAS_ARRANGE;
    case CanvasEventType::ArrangeCurrentBed:
        return EVT_GLCANVAS_ARRANGE_CURRENT_BED;
    case CanvasEventType::IncreaseInstances:
        return EVT_GLCANVAS_INCREASE_INSTANCES;
    case CanvasEventType::InstanceMoved:
        return EVT_GLCANVAS_INSTANCE_MOVED;
    case CanvasEventType::InstanceRotated:
        return EVT_GLCANVAS_INSTANCE_ROTATED;
    case CanvasEventType::InstanceScaled:
        return EVT_GLCANVAS_INSTANCE_SCALED;
    case CanvasEventType::InstanceMirrored:
        return EVT_GLCANVAS_INSTANCE_MIRRORED;
    case CanvasEventType::ResetSkew:
        return EVT_GLCANVAS_RESET_SKEW;
    case CanvasEventType::ScheduleBackgroundProcess:
        return EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS;
    case CanvasEventType::ForceInvalidateSlice:
        return EVT_GLCANVAS_FORCE_INVALIDATE_SLICE;
    case CanvasEventType::ForceUpdate:
        return EVT_GLCANVAS_FORCE_UPDATE;
    case CanvasEventType::RightClick:
        return EVT_GLCANVAS_RIGHT_CLICK;
    case CanvasEventType::PreviewLeftClick:
        return EVT_GLCANVAS_PREVIEW_LEFT_CLICK;
    case CanvasEventType::QuestionMark:
        return EVT_GLCANVAS_QUESTION_MARK;
    case CanvasEventType::MouseDraggingStarted:
        return EVT_GLCANVAS_MOUSE_DRAGGING_STARTED;
    case CanvasEventType::MouseDraggingFinished:
        return EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED;
    case CanvasEventType::WipeTowerTouched:
        return EVT_GLCANVAS_WIPETOWER_TOUCHED;
    case CanvasEventType::EnableActionButtons:
        return EVT_GLCANVAS_ENABLE_ACTION_BUTTONS;
    case CanvasEventType::Tab:
        return EVT_GLCANVAS_TAB;
    case CanvasEventType::CollapseSidebar:
        return EVT_GLCANVAS_COLLAPSE_SIDEBAR;
    case CanvasEventType::ResetGizmos:
        return EVT_GLCANVAS_RESETGIZMOS;
    case CanvasEventType::SlidersManipulation:
        return EVT_GLCANVAS_SLIDERS_MANIPULATION;
    case CanvasEventType::ResetLayerHeightProfile:
        return EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE;
    case CanvasEventType::AdaptiveLayerHeightProfile:
        return EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE;
    case CanvasEventType::SmoothLayerHeightProfile:
        return EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE;
    case CanvasEventType::UpdateGeometry:
        return EVT_GLCANVAS_UPDATE_GEOMETRY;
    case CanvasEventType::UpdateBedShape:
        return EVT_GLCANVAS_UPDATE_BED_SHAPE;
    case CanvasEventType::Undo:
        return EVT_GLCANVAS_UNDO;
    case CanvasEventType::Redo:
        return EVT_GLCANVAS_REDO;
    case CanvasEventType::ToolbarAdd:
        return EVT_GLTOOLBAR_ADD;
    case CanvasEventType::ToolbarDelete:
        return EVT_GLTOOLBAR_DELETE;
    case CanvasEventType::ToolbarDeleteAll:
        return EVT_GLTOOLBAR_DELETE_ALL;
    case CanvasEventType::ToolbarArrange:
        return EVT_GLTOOLBAR_ARRANGE;
    case CanvasEventType::ToolbarArrangeCurrentBed:
        return EVT_GLTOOLBAR_ARRANGE_CURRENT_BED;
    case CanvasEventType::ToolbarCopy:
        return EVT_GLTOOLBAR_COPY;
    case CanvasEventType::ToolbarPaste:
        return EVT_GLTOOLBAR_PASTE;
    case CanvasEventType::ToolbarMore:
        return EVT_GLTOOLBAR_MORE;
    case CanvasEventType::ToolbarFewer:
        return EVT_GLTOOLBAR_FEWER;
    case CanvasEventType::ToolbarSplitObjects:
        return EVT_GLTOOLBAR_SPLIT_OBJECTS;
    case CanvasEventType::ToolbarSplitVolumes:
        return EVT_GLTOOLBAR_SPLIT_VOLUMES;
    case CanvasEventType::ToolbarLayersEditing:
        return EVT_GLTOOLBAR_LAYERSEDITING;
    case CanvasEventType::ViewToolbar3D:
        return EVT_GLVIEWTOOLBAR_3D;
    case CanvasEventType::ViewToolbarPreview:
        return EVT_GLVIEWTOOLBAR_PREVIEW;
    case CanvasEventType::TakeSnapshot:
        return EVT_GLCANVAS_TAKE_SNAPSHOT;
    case CanvasEventType::TakeSnapshotSelection:
        return EVT_GLCANVAS_TAKE_SNAPSHOT_SELECTION;
    case CanvasEventType::UpdateInfoItems:
        return EVT_GLCANVAS_UPDATE_INFO_ITEMS;
    case CanvasEventType::ObjListSelectionChanged:
        return EVT_GLCANVAS_OBJ_LIST_SELECTION_CHANGED;
    case CanvasEventType::UpdateSelections:
        return EVT_GLCANVAS_UPDATE_SELECTIONS;
    case CanvasEventType::HideSliceButton:
        return EVT_GLCANVAS_HIDE_SLICE_BUTTON;
    case CanvasEventType::ShowSliceButton:
        return EVT_GLCANVAS_SHOW_SLICE_BUTTON;
    case CanvasEventType::ToggleRenderStatisticDialog:
        return EVT_GLCANVAS_TOGGLE_RENDER_STATISTIC_DIALOG;
    case CanvasEventType::ShowAutoslicingActionButtons:
        return EVT_GLCANVAS_SHOW_AUTOSLICING_ACTION_BUTTONS;
    case CanvasEventType::SwitchToAutoslicingMode:
        return EVT_GLCANVAS_SWITCH_TO_AUTOSLICING;
    case CanvasEventType::SwitchFromAutoslicingMode:
        return EVT_GLCANVAS_SWITCH_FROM_AUTOSLICING;
    case CanvasEventType::ObjectListChanged:
        return EVT_GLCANVAS_OBJECT_LIST_CHANGED;
    case CanvasEventType::CollapseSidebarToggle:
        return EVT_GLCANVAS_COLLAPSE_SIDEBAR_TOGGLE;
    case CanvasEventType::MinimizeWindow:
        return EVT_GLCANVAS_MINIMIZE_WINDOW;
    case CanvasEventType::TakeGizmoSnapshot:
        return EVT_GLCANVAS_TAKE_GIZMO_SNAPSHOT;
    case CanvasEventType::ManipulationDirty:
        return EVT_GLCANVAS_MANIPULATION_DIRTY;
    case CanvasEventType::ManipulationUpdateAndShow:
        return EVT_GLCANVAS_MANIPULATION_UPDATE_AND_SHOW;
    case CanvasEventType::ChangedObject:
        return EVT_GLCANVAS_CHANGED_OBJECT;
    case CanvasEventType::SetPlaterDirty:
        return EVT_GLCANVAS_SET_PLATER_DIRTY;
    case CanvasEventType::PlaterUpdate:
        return EVT_GLCANVAS_PLATER_UPDATE;
    case CanvasEventType::TabUpdateDirty:
        return EVT_GLCANVAS_TAB_UPDATE_DIRTY;
    case CanvasEventType::ObjectListUpdateAfterUndoRedo:
        return EVT_GLCANVAS_OBJ_LIST_UPDATE_AFTER_UNDO_REDO;
    case CanvasEventType::ObjectListAddObject:
        return EVT_GLCANVAS_OBJ_LIST_ADD_OBJECT;
    case CanvasEventType::ObjectListUpdateItemErrorIcon:
        return EVT_GLCANVAS_OBJ_LIST_UPDATE_ITEM_ERROR_ICON;
    case CanvasEventType::ScaleSelectionToFitPrintVolume:
        return EVT_GLCANVAS_SCALE_SELECTION_TO_FIT_PRINT_VOLUME;
    }
    assert(false);
    return wxEVT_NULL;
}

// --- CanvasEventPoster_wx ---

void CanvasEventPoster_wx::postEvent(CanvasEventType type)
{
    SimpleEvent evt(canvas_event_to_wx(type));
    evt.SetEventObject(m_target);
    wxPostEvent(m_target, evt);
}

void CanvasEventPoster_wx::postEvent(CanvasEventType type, int data)
{
    Event<int> evt(canvas_event_to_wx(type), data);
    evt.SetEventObject(m_target);
    wxPostEvent(m_target, evt);
}

void CanvasEventPoster_wx::postEvent(CanvasEventType type, bool data)
{
    Event<bool> evt(canvas_event_to_wx(type), data);
    evt.SetEventObject(m_target);
    wxPostEvent(m_target, evt);
}

void CanvasEventPoster_wx::postEvent(CanvasEventType type, float data)
{
    Event<float> evt(canvas_event_to_wx(type), data);
    evt.SetEventObject(m_target);
    wxPostEvent(m_target, evt);
}

void CanvasEventPoster_wx::postEvent(CanvasEventType type, const std::pair<Vec2d, bool> &data)
{
    Event<std::pair<Vec2d, bool>> evt(canvas_event_to_wx(type), data);
    evt.SetEventObject(m_target);
    wxPostEvent(m_target, evt);
}

void CanvasEventPoster_wx::postEvent(CanvasEventType type, const std::array<Vec3d, 2> &data)
{
    ArrayEvent<Vec3d, 2> evt(canvas_event_to_wx(type), data);
    evt.SetEventObject(m_target);
    wxPostEvent(m_target, evt);
}

void CanvasEventPoster_wx::postEvent(CanvasEventType type, const HeightProfileSmoothingParams &data)
{
    Event<HeightProfileSmoothingParams> evt(canvas_event_to_wx(type), data);
    evt.SetEventObject(m_target);
    wxPostEvent(m_target, evt);
}

void CanvasEventPoster_wx::postEvent(CanvasEventType type, const std::string &data)
{
    Event<std::string> evt(canvas_event_to_wx(type), data);
    evt.SetEventObject(m_target);
    wxPostEvent(m_target, evt);
}

void CanvasEventPoster_wx::postKeyEvent(CanvasEventType type, int key_code, int modifiers)
{
    wxKeyEvent evt(canvas_event_to_wx(type));
    evt.SetEventObject(m_target);
    evt.m_keyCode = key_code;
    evt.m_shiftDown = (modifiers & wxMOD_SHIFT) != 0;
    evt.m_controlDown = (modifiers & wxMOD_CONTROL) != 0;
    evt.m_altDown = (modifiers & wxMOD_ALT) != 0;
    evt.m_metaDown = (modifiers & wxMOD_META) != 0;
    wxPostEvent(m_target, evt);
}

// --- SlicingEventPoster_wx ---

void SlicingEventPoster_wx::postSlicingStatus(const PrintBase::SlicingStatus &status)
{
    wxQueueEvent(m_target, new SlicingStatusEvent(m_slicing_update_id, 0, status));
}

void SlicingEventPoster_wx::postSlicingCompleted(int timestamp)
{
    auto *evt = new wxCommandEvent(m_slicing_completed_id);
    evt->SetInt(timestamp);
    wxQueueEvent(m_target, evt);
}

void SlicingEventPoster_wx::postExportBegan()
{
    wxQueueEvent(m_target, new wxCommandEvent(m_export_began_id));
}

void SlicingEventPoster_wx::postProcessCompleted(SlicingCompletedStatus status, std::exception_ptr exception)
{
    SlicingProcessCompletedEvent::StatusType wx_status;
    switch (status)
    {
    case SlicingCompletedStatus::Finished:
        wx_status = SlicingProcessCompletedEvent::Finished;
        break;
    case SlicingCompletedStatus::Cancelled:
        wx_status = SlicingProcessCompletedEvent::Cancelled;
        break;
    case SlicingCompletedStatus::Error:
        wx_status = SlicingProcessCompletedEvent::Error;
        break;
    }
    auto *evt = new SlicingProcessCompletedEvent(m_finished_id, 0, wx_status, exception);
    wxQueueEvent(m_target, evt);
}

void SlicingEventPoster_wx::callOnUIThreadAsync(std::function<void()> task)
{
    wxTheApp->CallAfter(std::move(task));
}

} // namespace DSKY
