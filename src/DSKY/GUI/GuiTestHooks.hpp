///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

namespace DSKY
{

class GUI_App;

// The GUI's test hooks, compiled into debug builds only (PREFLIGHT_TEST_HOOKS): environment variables
// that drive the application as a user would (open a panel, switch categories and extruders, change
// the extruder count, close the frame) and write the settings registry and the widget probe. Without
// the variables nothing happens.
//
// PREFLIGHT_DUMP_SIDEBAR=<dir>              the settings registry of every surface, then exit
// PREFLIGHT_GUI_PROBE=<file>                the per-widget GDI and USER cost table, then exit
// PREFLIGHT_SIDEBAR_TAB=<index>[,edit]      the tabbed sidebar shows that tab
// PREFLIGHT_OPEN_OVERRIDES=<open>[,<close>] the first object's override panel opens, the frame closes
// PREFLIGHT_SELECT_PRINTER=<preset>         the printer is switched once the start-up prebuild is done
// PREFLIGHT_CYCLE_EXTRUDERS=<rounds>        the Printer panel's Extruders section cycles its extruders
// PREFLIGHT_CYCLE_OVERRIDES=<rounds>        the open override panel cycles its categories and reopens
// PREFLIGHT_SET_EXTRUDERS=<count>,<ms>[,snapshot] the printer's extruder count is set
// PREFLIGHT_UNDO_AT_MS=<ms>                 one undo, as Ctrl+Z does
// PREFLIGHT_SELECT_OBJECTS_AT_MS=<ms>       selection changes as clicks in the object list and the 3D view
// PREFLIGHT_MOVE_OBJECT_AT_MS=<ms>          the first object moved 20 mm along X as a drag in the 3D view ends
// PREFLIGHT_ADD_SHAPE_AT_MS=<ms>[,<shape>]  a shape added as the bed's context menu adds it (Box by default)
// PREFLIGHT_PROBE_OBJECT_X_AT_MS=<ms>       the first object's X traced at that time
// PREFLIGHT_OPEN_ABOUT_AT_MS=<ms>           the About dialog opens
// PREFLIGHT_END_SESSION_AT_MS=<ms>          the session ends as at a logoff (Windows)
// PREFLIGHT_REBUILD_OVERRIDES_AT_MS=<ms>    the override panel rebuilds inside its start-up prebuild
// PREFLIGHT_RENDER_STEPS=<file>             frames captured under the render settings each line names
// PREFLIGHT_RENDER_OUT=<dir>                  (to <dir>/<name>.png and .txt, then exit)
// PREFLIGHT_VSYNC=0                         the swap does not wait for the display (frame timing)
// PREFLIGHT_PREVIEW_PREPARE=0               no slice prepares its Preview: the load prepares on the UI thread
//                                             (Plater.cpp reads it)
// PREFLIGHT_UI_STALL_PROBE=0                the UI stall probe stays off: ui_max_stall_ms reads 0 (GuiBudget.cpp)
namespace GuiTestHooks
{

// Once the main window is shown, before the start-up's deferred work runs
void on_window_up(GUI_App &app);

// After the command-line project load has returned
void on_loaded(GUI_App &app);

} // namespace GuiTestHooks
} // namespace DSKY
