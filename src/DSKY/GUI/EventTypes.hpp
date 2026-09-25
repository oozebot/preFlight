///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

namespace DSKY
{

// Event types for the canvas and viewport subsystem.
// The wx implementation maps these to wxEventType values via EventBridge_wx.
enum class CanvasEventType
{
    // Object/selection events
    ObjectSelect,
    RemoveObject,
    SelectAll,
    ReloadFromDisk,

    // Arrangement
    Arrange,
    ArrangeCurrentBed,

    // Instance manipulation
    IncreaseInstances,
    InstanceMoved,
    InstanceRotated,
    InstanceScaled,
    InstanceMirrored,
    ResetSkew,

    // Background process triggers
    ScheduleBackgroundProcess,
    ForceInvalidateSlice,
    ForceUpdate,

    // Interaction
    RightClick,
    PreviewLeftClick,
    QuestionMark,
    MouseDraggingStarted,
    MouseDraggingFinished,
    WipeTowerTouched,

    // UI state
    EnableActionButtons,
    Tab,
    CollapseSidebar,
    ResetGizmos,

    // Layer height
    SlidersManipulation,
    ResetLayerHeightProfile,
    AdaptiveLayerHeightProfile,
    SmoothLayerHeightProfile,

    // Geometry
    UpdateGeometry,
    UpdateBedShape,

    // Undo/redo
    Undo,
    Redo,

    // Toolbar actions
    ToolbarAdd,
    ToolbarDelete,
    ToolbarDeleteAll,
    ToolbarArrange,
    ToolbarArrangeCurrentBed,
    ToolbarCopy,
    ToolbarPaste,
    ToolbarMore,
    ToolbarFewer,
    ToolbarSplitObjects,
    ToolbarSplitVolumes,
    ToolbarLayersEditing,

    // View switching
    ViewToolbar3D,
    ViewToolbarPreview,

    // Undo/redo snapshots
    TakeSnapshot,
    TakeSnapshotSelection,

    // Object list updates
    UpdateInfoItems,
    ObjListSelectionChanged,
    UpdateSelections,

    // UI state actions
    HideSliceButton,
    ShowSliceButton,
    ToggleRenderStatisticDialog,
    ShowAutoslicingActionButtons,
    SwitchToAutoslicingMode,
    SwitchFromAutoslicingMode,
    ObjectListChanged,
    CollapseSidebarToggle,
    MinimizeWindow,

    TakeGizmoSnapshot,
    ManipulationDirty,
    ManipulationUpdateAndShow,
    ChangedObject,
    SetPlaterDirty,
    PlaterUpdate,
    TabUpdateDirty,
    ObjectListUpdateAfterUndoRedo,
    ObjectListAddObject,
    ObjectListUpdateItemErrorIcon,
    ScaleSelectionToFitPrintVolume,
};

// Status for background slicing process completion
enum class SlicingCompletedStatus
{
    Finished,
    Cancelled,
    Error
};

} // namespace DSKY
