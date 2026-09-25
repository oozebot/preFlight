///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2021 Lukáš Hejl @hejllukas, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

// events passed from GLCanvas3D to the gizmos
namespace DSKY
{

enum class GizmoEventType : unsigned char
{
    LeftDown = 1,
    LeftUp,
    RightDown,
    Dragging,
    Delete,
    SelectAll,
    ShiftUp,
    AltUp,
    ApplyChanges,
    DiscardChanges,
    AutomaticGeneration,
    ManualEditing,
    MouseWheelUp,
    MouseWheelDown,
    ResetClippingPlane
};

} // namespace DSKY

#include "DSKY/GUI/Gizmos/GLGizmoMove.hpp"
#include "DSKY/GUI/Gizmos/GLGizmoScale.hpp"
#include "DSKY/GUI/Gizmos/GLGizmoRotate.hpp"
#include "DSKY/GUI/Gizmos/GLGizmoFlatten.hpp"
#include "DSKY/GUI/Gizmos/GLGizmoFdmSupports.hpp"
#include "DSKY/GUI/Gizmos/GLGizmoFuzzySkin.hpp"
#include "DSKY/GUI/Gizmos/GLGizmoCut.hpp"
