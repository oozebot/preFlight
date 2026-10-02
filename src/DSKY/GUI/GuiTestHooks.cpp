///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "GuiTestHooks.hpp"

#include "GLCanvas3D.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectSettings.hpp"
#include "GUI_Preview.hpp"
#include "GuiBudget.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "RenderDiagnostics.hpp"
#include "RenderPassTimer.hpp"
#include "ScenePasses.hpp"
#include "Sidebar.hpp"
#include "Tab.hpp"
#include "luminary/io/png/PNGReadWrite.hpp"
#include "luminary/model/beds/MultipleBeds.hpp"
#include "luminary/presets/app_config/AppConfig.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/timer.h>
#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace DSKY
{
namespace GuiTestHooks
{
namespace
{

// Clicks come this far apart in the cycles, so each step settles (its layout and paint) first
constexpr int STEP_MS = 150;

// PREFLIGHT_OPEN_OVERRIDES with an open delay of 0: the open waits for the command-line load
bool s_open_after_load = false;

// The variable's value, empty when it is not set
std::string env(const char *name)
{
    const char *value = std::getenv(name);
    return value != nullptr ? value : "";
}

// Runs `fn` once, `ms` from now, from a one-shot timer that is deleted after it fires
void after_ms(int ms, std::function<void()> fn)
{
    auto *timer = new wxTimer();
    timer->Bind(wxEVT_TIMER,
                [timer, fn = std::move(fn)](wxTimerEvent &)
                {
                    timer->Stop();
                    wxGetApp().CallAfter([timer]() { delete timer; });
                    fn();
                });
    timer->StartOnce(std::max(1, ms));
}

// Runs `fn` once the override panel's rows exist (at once when they do)
void when_prebuilt(std::function<void()> fn)
{
    if (wxGetApp().plater() != nullptr)
        wxGetApp().obj_settings()->on_prebuilt(std::move(fn));
}

// Closes the main frame the way a forced close does, whatever dialog is open
void close_frame()
{
    if (wxGetApp().mainframe != nullptr)
        wxGetApp().mainframe->Close(true);
}

#ifdef _WIN32
// Ends the session as Windows does at a logoff: the query to every top-level window, then the end.
// Both are sent from this one event, so a dialog's modal loop is still on the stack when the end
// arrives (a critical shutdown that does not wait).
void end_session()
{
    std::vector<HWND> windows;
    for (wxWindow *window : wxTopLevelWindows)
        windows.push_back((HWND) window->GetHWND());
    bool may_end = true;
    for (HWND hwnd : windows)
        if (::IsWindow(hwnd) && !::SendMessage(hwnd, WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF))
            may_end = false;
    for (HWND hwnd : windows)
        if (::IsWindow(hwnd))
            ::SendMessage(hwnd, WM_ENDSESSION, may_end ? TRUE : FALSE, ENDSESSION_LOGOFF);
}
#endif

// The first object's X in the trace, in micrometres
void trace_first_object_x(const char *tag)
{
    const Model &model = wxGetApp().plater()->model();
    if (!model.objects.empty() && !model.objects.front()->instances.empty())
        GuiBudget::measure(tag, std::llround(model.objects.front()->instances.front()->get_offset().x() * 1000.));
}

// PREFLIGHT_MOVE_OBJECT_AT_MS: the first object moved 20 mm along X as a drag in the 3D view ends it
// (the selection translated, then GLCanvas3D::do_move with its undo snapshot)
void move_first_object()
{
    if (wxGetApp().plater() == nullptr || wxGetApp().plater()->model().objects.empty())
        return;
    GLCanvas3D *canvas = wxGetApp().plater()->canvas3D();
    Selection &selection = canvas->get_selection();
    trace_first_object_x("model.first_object_x.before_move");
    selection.add_object(0);
    selection.setup_cache();
    TransformationType trafo_type;
    trafo_type.set_relative();
    selection.translate(Vec3d(20., 0., 0.), trafo_type);
    canvas->do_move("Move Object");
    trace_first_object_x("model.first_object_x.after_move");
}

// PREFLIGHT_SELECT_OBJECTS_AT_MS: step `step` of the selection changes, then the next one later
void select_as_clicks(int step)
{
    if (wxGetApp().plater() == nullptr)
        return;
    ObjectList *list = wxGetApp().obj_list();
    Selection &selection = wxGetApp().plater()->canvas3D()->get_selection();
    switch (step)
    {
    case 0: // a click on the first object, as the list's selection event runs it
        if (list->GetModel()->IsEmpty())
            return;
        list->UnselectAll();
        list->Select(list->GetModel()->GetItemById(0));
        list->selection_changed();
        break;
    case 1:
        list->select_all();
        break;
    case 2:
        selection.remove_all();
        break;
    case 3:
        selection.add_object(0);
        break;
    default:
        return;
    }
    after_ms(STEP_MS, [step]() { select_as_clicks(step + 1); });
}

// Selects the first object and opens its override panel
void open_first_object_overrides()
{
    ObjectList *list = wxGetApp().plater() != nullptr ? wxGetApp().obj_list() : nullptr;
    if (list != nullptr && !list->GetModel()->IsEmpty())
        list->open_overrides(list->GetModel()->GetItemById(0));
}

// PREFLIGHT_CYCLE_OVERRIDES=<rounds>: the open override panel selects each of its categories in turn,
// then closes and opens again, `rounds` times; each switch and each open is traced
// (overrides.category.* and overrides.open.*)
void cycle_overrides(size_t step, size_t rounds)
{
    if (wxGetApp().plater() == nullptr)
        return;
    ObjectSettings *panel = wxGetApp().obj_settings();
    const size_t categories = size_t(std::max(0, panel->category_count()));
    if (categories == 0 || step >= (categories + 1) * rounds)
        return;
    // Each round: categories 1 .. n-1, back to the first, then a close and an open
    const size_t in_round = step % (categories + 1);
    if (in_round < categories)
        panel->select_category(int((in_round + 1) % categories));
    else
    {
        wxGetApp().obj_list()->close_overrides();
        open_first_object_overrides();
    }
    after_ms(STEP_MS, [step, rounds]() { cycle_overrides(step + 1, rounds); });
}

void cycle_overrides_from_env()
{
    const int rounds = std::atoi(env("PREFLIGHT_CYCLE_OVERRIDES").c_str());
    if (rounds > 0)
        wxGetApp().CallAfter([rounds]() { cycle_overrides(0, size_t(rounds)); });
}

// PREFLIGHT_CYCLE_EXTRUDERS=<rounds>: the Printer panel's Extruders section switches to every extruder
// in turn and back to the first, `rounds` times; each switch is traced (printer.extruder_switch.*).
// The override cycle follows, so their timings never overlap.
void cycle_printer_extruders(size_t next, size_t rounds)
{
    if (wxGetApp().plater() == nullptr)
        return;
    Sidebar &sidebar = wxGetApp().sidebar();
    const size_t count = sidebar.printer_extruders_count();
    if (count < 2 || next > count * rounds)
    {
        cycle_overrides_from_env();
        return;
    }
    sidebar.select_printer_extruder(next % count);
    after_ms(STEP_MS, [next, rounds]() { cycle_printer_extruders(next + 1, rounds); });
}

// The open, then the cycles one step later, so the open's layout and paint are not measured as the
// first step's
void open_then_cycle()
{
    open_first_object_overrides();
    after_ms(STEP_MS,
             []()
             {
                 const int rounds = std::atoi(env("PREFLIGHT_CYCLE_EXTRUDERS").c_str());
                 if (rounds > 0)
                     cycle_printer_extruders(1, size_t(rounds));
                 else
                     cycle_overrides_from_env();
             });
}

// PREFLIGHT_SELECT_PRINTER, as its combo switches it, then the open one step later, as a click would
// come: the timer fires once the switch's own layout and paint are done, so they are not measured as
// the open's. The open still waits for the panel's rows, so a build the switch started and never
// finished leaves the panel closed (the trace then has no open).
void switch_printer_then_open(const std::string &printer)
{
    if (!printer.empty())
        if (Tab *tab = wxGetApp().get_tab(Luminary::Preset::TYPE_PRINTER))
            tab->select_preset(printer);
    if (s_open_after_load)
        after_ms(STEP_MS, []() { when_prebuilt(open_then_cycle); });
}

// PREFLIGHT_RENDER_STEPS=<file> with PREFLIGHT_RENDER_OUT=<dir>: frames rendered under the settings each line of
// the file names, captured without the overlays as <dir>/<name>.png, with <dir>/<name>.txt holding what the
// frame rendered with (the render diagnostics) and, for bench=<frames>, the frame times of that many frames
// orbiting 0.2 degrees apart. One capture per line:
//     <name> [view=prepare|preview] [strip=shown|hidden] [lighting=basic|enhanced|full|auto]
//            [ssaa=off|1.5|2|4] [offscreen=0|1] [cam=iso|top|front|...] [fit=objects|bed]
//            [az=<degrees>] [zen=<degrees>] [zoom=<factor>] [ss=0|1] [pf=0|1] [aon=0|1] [bench=<frames>]
//            [bench_pass=1] [pfdump=1] [cull=0|1] [chunkcull=0|1] [occl=0|1] [occl_cap=<segments>]
//            [range=<lo>,<hi>] [probe=1] [hide=<names>] [slide=<n>]
//            [occl_shadow_all=<segments>|default] [occl_shadow_merge=0|1] [glrelease=1]
// bench_pass=1 with bench then renders as many frames again with the GPU finished at each pass boundary and writes
// bench_pass_frames, pass_<name>_ms (the median over those frames of each pass's time, 0 in a frame without it)
// and pass_sum_ms (the median of the per-frame sums); bench_median_ms stays the timed total of the first run.
// slide=<n>, after the capture (and the bench), moves the layer slider's upper thumb down one layer at a time n times
// (no lower than the lower thumb) and back up as many, each move through the slider as a drag makes it, then one frame
// finished on the GPU, and writes slide_steps (the moves made), slide_ms_avg and slide_ms_max (a step: the move with the
// host's follow-up and the frame), and per step on average its parts, which add up to it: slide_view_range_ms (the
// view ranges), slide_colors_ms with slide_colors_bytes_avg (the color buffer updates and the bytes they wrote),
// slide_lists_ms (the enabled lists, or the option list alone with its upload while the segment list is deferred),
// slide_chunks_ms (the chunk set of the segment list), slide_upload_ms with slide_upload_bytes_avg (the built lists'
// uploads), slide_filter_ms (the whole print's structure: its flags and the filter's application), wherever in the step
// the viewer did them;
// slide_moves_slider_ms (the host's moves slider update without that viewer work) and slide_frame_ms (the frame
// without it: the occlusion step and the draw); and slide_lists_deferred (the list updates over the run that deferred
// the segment list) and slide_occl_rounds_avg (the camera view's box test rounds per step).
// A line keeps the earlier lines' values for the keys it does not name (bench, bench_pass, pfdump, probe, slide and
// glrelease excepted); a key this list does not name is ignored.
// offscreen=1 renders through the offscreen target at scale 1, the path inset viewports took before the scene
// rendered into the window. ss=0|1 turns the toolpaths' per-sample shading on multisampled targets off or on (on until a line
// names it); <name>.txt records it as sample_shading, with sample_shading_active telling whether the captured
// frame's toolpath draw used it, sample_shading_gated whether the gate kept it off and sample_shading_reason why
// ("prefilter active", "pixel budget" or "sections resolvable"; empty when it was on or did not apply). pf=0|1 sets the app-config key toolpath_prefilter (the Performance preference "Smooth
// layer lines") and aon=0|1 the hidden key ao_toolpath_normals (the Full tier's SSAO normal of toolpath
// pixels rebuilt from depth); both stay as the config has them until a line names them, and <name>.txt
// records prefilter_requested, prefilter_active, prefilter_reason and ao_toolpath_normals (the depth step in
// G-buffer texels, 0 when off), and the prefilter's load-time neighbour search as pf_wall_vertices,
// pf_found_vertices (wall vertices that found the bead above), pf_excluded_segments (wall segments left out for a
// non-finite or out-of-range attribute), pf_unmatched_segments (wall segments that took a bead that does not
// continue their surface), pf_search_threads, pf_search_ms and pf_search_error (why the search failed,
// empty when it ran). pfdump=1 also writes <dir>/<name>_pf.csv with x,y,z,flags,offset_x,offset_y of every wall
// vertex (flags 1 wall, 2 found; the offset in mm from the vertex to the bead above); with a pfdump line in the file
// the viewer keeps that per-vertex data after the load. cull=0|1 turns the toolpaths' sealed bead culling off or on
// (on until a line names it; set on the viewer, not in the config); <name>.txt records cull_enabled, cull_active
// (whether the captured frame's segment list was culled), cull_segments_drawn and cull_segments_total (the segments
// that list drew out of those enabled before culling), and the load-time pass as cull_ms, cull_resolution_mm,
// cull_threads and cull_error (why it failed, empty when it ran). chunkcull=0|1 turns the per-frame chunk culling
// (view frustum) off or on the same way; <name>.txt records chunkcull_enabled,
// chunkcull_active (whether the visible pass drew a chunk selection), chunks_total, chunks_drawn,
// chunk_segments_drawn and chunk_shadow_segments_drawn (the segments the visible and the shadow pass drew),
// chunk_build_ms (building the chunks of the segment list) and chunk_select_ms (the last camera selection). Every
// <name>.txt also records, since the load, list_upload_failures (the
// enabled list and chunk selection uploads whose buffer did not take the data: a GL error, or a buffer size other than
// the bytes handed over), list_upload_retries (the enabled list rebuilds made because of one, at most one per failed
// update) and list_upload_reason (why the last one failed, empty when none did; 0, 0 and empty by design), and the
// chunk structure of the whole print the load built (desktop OpenGL only; nothing is drawn from it yet): pc_valid,
// pc_chunks, pc_subcells, pc_segments, pc_bytes and pc_build_ms, and of the last enabled list update pc_filter_ms
// (flagging the sub-cells that can hold an enabled segment, with the filter's application when one ran) and
// pc_candidate_subcells (the sub-cells flagged), and of the last application of the filter to the structure
// (occlusion culling on) pc_apply_ms and
// pc_applied_subcells (the sub-cells partitioned again). occl=0|1 turns occlusion culling off or on the same way (on,
// as the application has it, until a line names it): each camera pass and the shadow pass draws the sub-cells of that
// structure whose boxes pass against the depth of what was drawn for its view. occl_cap=<segments> sets the most
// segments of a view's last draw set (nearest first) a step draws as its first occluders (0, no cap, until a line
// names it). occl_shadow_all=<segments> sets the most enabled segments the shadow view draws whole, with no step (0
// never; the viewer's built-in limit until a line names it, and again for occl_shadow_all=default).
// occl_shadow_merge=0|1 turns off or on the shadow pass's merge after a step: the step's occluder depth written into
// the shadow map and only the residual drawn in place of the draw set (on until a line names it). Outside a
// capture, PREFLIGHT_OCCLUSION=0 turns occlusion culling off and
// PREFLIGHT_OCCLUSION_CAP=<segments> sets the cap when the
// G-code viewer initializes (GCodeViewer::init); a capture's lines set both from their own keys. Every <name>.txt
// records occl_enabled, occl_cap, occl_shadow_all_max (the shadow view's limit in effect), of the camera view's last
// pass occl_active, occl_reason (why it did not apply, empty when it did), occl_rounds (box tests run), occl_points
// (boxes tested: chunks, then sub-cells of the chunks that passed, over the rounds), occl_visible_subcells,
// occl_occluder_segments (drawn as occluders, over the rounds), occl_segments_drawn (the draw set) and occl_ms (the
// step's time, 0 when the pass took the result of an unchanged view, whose other figures are those of the step that
// made it), of the shadow view's occl_shadow_active, occl_shadow_all (1 when it drew the whole enabled set with no
// step), occl_shadow_reason, occl_shadow_rounds, occl_shadow_segments_drawn, occl_shadow_ms, occl_shadow_merge (the
// switch), occl_shadow_merged (1 when the last shadow pass merged its step's depth and drew only the residual) and
// occl_shadow_residual_segments (the residual its step wrote, 0 for a stored or whole-set result), and since the load
// occl_steps and occl_cached (steps run, passes that took a step's stored result; a whole enabled set is in neither),
// occl_map_fallbacks and occl_map_fallback_reason (lists written through a copy because their buffer could not be
// mapped, and why the last one was: map failed or unmap failed; 0 and empty by design), occl_shadow_merge_fallbacks
// and occl_shadow_merge_reason (shadow passes whose step could not merge and drew the whole draw set, and why the
// last one could not; 0 and empty by design without an occluder cap). A bench line also records the
// step over its bench frames: occl_bench_steps and occl_bench_cached (the camera view's steps run and stored results
// taken), per step occl_bench_rounds_avg, occl_bench_points_avg, occl_bench_segments_avg (the draw set),
// occl_bench_occluders_avg (segments drawn as occluders, every round), occl_bench_visible_avg (sub-cells that passed),
// occl_bench_new_segments_avg (the enabled segments of the sub-cells that passed the first test and were not
// predicted) and occl_bench_ms_avg, and per step by part occl_bench_emit_ms (choosing, writing and uploading
// occluders), occl_bench_depth_ms (drawing them), occl_bench_pyramid_ms, occl_bench_test_ms (the box test draws),
// occl_bench_readback_ms (the readbacks, which wait for the GPU) and occl_bench_list_ms (ordering, writing and
// uploading the draw set); the draw parts time the GL calls, the readback the GPU's work before it. Then
// occl_bench_shadow_steps, occl_bench_shadow_ms_avg, occl_bench_shadow_all (its passes that drew the whole enabled
// set, written or kept), occl_bench_shadow_merged (its passes that merged their step's depth) and
// occl_bench_shadow_residual_avg (the residual's segments per merged pass) for the shadow view.
// range=<lo>,<hi> moves the Preview's layer slider
// to those fractions of the layer count (0,1 the full range until a line names it); <name>.txt records the viewer's
// displayed layers as layers_range=<first>,<last> and layers_count. moves=<lo>,<hi> then sets the Preview's moves slider
// (the position within the top layer) to those fractions of its span, as a drag sets it, on every line after the layer
// range and before the feature types (0,1 the full span until a line names it; a value that is not two numbers from 0
// to 1 ends the run with <dir>/_error.txt); <name>.txt records the slider's thumbs and last position as
// moves_range=<lo>,<hi>,<max>. hide=<names> hides the feature types it names,
// comma separated, and shows every other one, each toggled as a click on its legend row toggles it (none hidden until
// a line names it; hide= or hide=none shows them all). The names are the extrusion roles in lower case with
// underscores: serpentine, serpentine_overhang, perimeter, external_perimeter, overhang_perimeter,
// interlocking_perimeter, internal_infill, solid_infill, top_solid_infill, ironing, bridge_infill, gap_fill, skirt,
// support_material, support_material_interface, wipe_tower, custom; any other name ends the run with
// <dir>/_error.txt. <name>.txt records the roles the viewer has hidden as hidden_roles, comma separated, empty when
// none. probe=1 compares the line's captured frame with what its camera sees: the camera's view of the whole chunk
// order (the enabled list; chunkcull=0 leaves none, and the probe does not run) rendered as segment ids at the size of
// the viewport the toolpaths drew to, so each pixel has its segment and sub-cell. <name>.txt then records probe_ran,
// probe_error (why it did not run), probe_width and probe_height, probe_list_segments (the enabled list),
// probe_drawn_segments (the segments the frame drew), probe_visible_segments (the segments that own a pixel of that
// view), probe_view_segments (the segments of the sub-cells that own a pixel), probe_view_chunks and
// probe_view_chunk_segments (the chunks that hold such a sub-cell, and their segments), probe_view_subcells and
// probe_view_pixels (the sub-cells that own a pixel, and the view's toolpath pixels), probe_chunk_culled_subcells and
// probe_chunk_culled_pixels (of them, those the frustum selection drops; none when the frame drew no selection) and
// probe_ms. Every probe also runs the occlusion box test against that view's own depth, through its farthest-depth
// pyramid: probe_box_chunks_tested, probe_box_chunks_visible and
// probe_box_chunks_outside (the chunk boxes tested, those that pass, those off the view or beyond the far plane; the
// rest are occluded), probe_box_chunk_segments (the segments of the chunks that pass), probe_box_subcells_tested (the
// sub-cells of those chunks), probe_box_subcells_visible and probe_box_segments (of them, those whose own box passes,
// and their segments: the draw set), probe_box_exact_subcells_visible and probe_box_exact_segments (the same tested
// against the full-resolution depth in place of the pyramid), probe_box_misses (sub-cells that own a pixel and are not
// in the draw set) and probe_box_ms (the box test's time, which probe_ms leaves out). Then its faces variant, which
// bounds a box's depth per pyramid texel by its front faces' planes, read where its rectangle covers at most 4 or 8
// texels per axis: probe_box4_subcells_visible, probe_box4_segments and probe_box4_misses (of the sub-cells tested
// above, those that pass with 4 texels, their segments, and those that own a pixel and do not pass),
// probe_box8_subcells_visible, probe_box8_segments and probe_box8_misses (the same with 8), probe_box4_chunks_visible,
// probe_box4_chunk_segments and probe_box4_chunk_misses (every chunk box with 4 texels: those that pass, their
// segments, and the sub-cells that own a pixel in a chunk that does not) and probe_box_faces_ms (the faces variant's
// time). Then the faces test on the chunk structure of the whole print with its tight boxes, under the frame's
// settings: probe_pc_chunks_tested and probe_pc_chunks_visible (4 texels on every chunk that holds a flagged
// sub-cell), probe_pc_subcells_tested and probe_pc_subcells_visible (8 texels on the flagged sub-cells of the chunks
// that pass), probe_pc_segments (the enabled segments of the sub-cells that pass: what that design would draw),
// probe_pc_misses (its sub-cells that own a pixel and do not pass: their chunk or their own box failed, or their flag
// is 0), probe_pc_enabled_segments (the enabled segments of every sub-cell, cull_segments_total for a structure that
// matches the list) and probe_pc_ms (that part's time). Then the segment test, each enabled segment of the sub-cells
// that pass tested on its own (its two ends' balls, 8 texels per axis): probe_seg_tested (the segments tested,
// probe_pc_segments), probe_seg_visible (those that pass: what a third level would draw), probe_seg_misses (the
// segments that own a pixel and do not pass: their sub-cell failed or their own test did; 0 by design),
// probe_seg_owners (the segments that own a pixel, probe_visible_segments) and probe_seg_ms (that part's time, which
// probe_pc_ms leaves out). With occl=1 and the frame drawn from the occlusion draw set
// (probe_occl 1), probe_occl_drawn_segments (its segments), probe_occl_missing_subcells, probe_occl_missing_pixels and
// probe_occl_missing_segments (the structure's sub-cells that own a pixel of the view and are not in the camera
// view's draw set, those pixels and the segments of them that own one: 0 by design; probe_occl_error says why they
// were not counted), and the cross-check of the GPU box test against the CPU rule on the culler's depth of one round
// from that draw set: probe_occl_xcheck_chunks and probe_occl_xcheck_subcells (compared),
// probe_occl_xcheck_chunk_gpu_only and probe_occl_xcheck_gpu_only (passed on the GPU alone, harmless),
// probe_occl_xcheck_chunk_cpu_only and probe_occl_xcheck_cpu_only (passed on the CPU alone, unsafe, expected 0) and
// probe_occl_xcheck_error (why it did not run). When the frame's Full lighting shadow pass runs, it checks the shadow
// map it drew against the light's view of the whole enabled list drawn by its program into a depth target of its own:
// probe_shadow_ran and probe_shadow_error (why it did not run; both unset when the frame had no shadow pass),
// probe_shadow_texels (the texels a bead covers), probe_shadow_missing_texels (those where the bead lies nearer than
// the map, missing from it: 0 by design) with probe_shadow_max_gap (the largest such gap in window depth),
// probe_shadow_segments_drawn (the segments the shadow pass drew), probe_shadow_list_segments (the enabled list's) and
// probe_shadow_ms. <dir>/<name>_probe.png shows the camera's view: black no toolpath, grey drawn, blue dropped by the
// chunk selection or left out of the occlusion draw set. glrelease=1 releases the GL context just before the line's
// keys are applied, so the state change the line makes (the feature types, the layer range) runs with no context
// current, as it does after the window lost focus; the settling frames and the capture then run as usual. Every
// <name>.txt records the last G-code load's phases in wall milliseconds: load_convert_ms, load_viewer_cpu_ms (the
// viewer's preparation outside its uploads, its sealed bead pass, its neighbour search, the chunk build of its list
// and the build of the whole print's chunk structure, pc_build_ms), load_gl_upload_ms with load_gl_upload_bytes,
// load_cog_ms, load_bounds_ms (with the bed check),
// load_gcode_window_ms, load_total_ms (with a preparation on the slicing thread, its time and the UI thread's part)
// and load_vertices; where the preparation ran as load_prepare_thread (slicing or ui) with load_prepare_fallback (why
// not on the slicing thread, empty when it was), the UI thread's part as load_install_ms, the Preview's work after the
// load as load_post_ms, the first frame drawing the toolpaths as load_first_frame_ms, and ui_max_stall_ms, the longest
// time the UI thread stayed away from its event loop from the start of the slice to that frame. The Preview is sliced
// once, before the first line that shows it; the frame closes after the last capture, leaving <dir>/_done.txt, or
// earlier with <dir>/_error.txt holding the reason when the slice leaves no toolpaths, a line's hide names an unknown
// feature type or a line's moves value is not two fractions.
struct RenderStep
{
    std::string name;
    std::map<std::string, std::string> keys;
};
std::vector<RenderStep> s_render_steps;
std::map<std::string, std::string> s_render_state;
std::string s_render_out;
// The viewer's built-in occl_shadow_all limit, read before the first line sets it
std::optional<size_t> s_occl_shadow_all_default;

constexpr double DEGREES = 3.14159265358979323846 / 180.0;

std::vector<RenderStep> read_render_steps(const std::string &path)
{
    std::vector<RenderStep> steps;
    boost::nowide::ifstream in(path);
    for (std::string line; std::getline(in, line);)
    {
        if (const size_t hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        RenderStep step;
        if (!(words >> step.name))
            continue;
        for (std::string word; words >> word;)
            if (const size_t eq = word.find('='); eq != std::string::npos)
                step.keys[word.substr(0, eq)] = word.substr(eq + 1);
        steps.push_back(std::move(step));
    }
    return steps;
}

std::string render_value(const std::string &key, const std::string &fallback)
{
    auto it = s_render_state.find(key);
    return it != s_render_state.end() ? it->second : fallback;
}

// The feature type names of the hide key, one per extrusion role but None, in the roles' order
struct RoleName
{
    const char *name;
    libvgcode::EGCodeExtrusionRole role;
};
constexpr RoleName ROLE_NAMES[] = {
    {"serpentine", libvgcode::EGCodeExtrusionRole::Serpentine},
    {"serpentine_overhang", libvgcode::EGCodeExtrusionRole::SerpentineOverhang},
    {"perimeter", libvgcode::EGCodeExtrusionRole::Perimeter},
    {"external_perimeter", libvgcode::EGCodeExtrusionRole::ExternalPerimeter},
    {"overhang_perimeter", libvgcode::EGCodeExtrusionRole::OverhangPerimeter},
    {"interlocking_perimeter", libvgcode::EGCodeExtrusionRole::InterlockingPerimeter},
    {"internal_infill", libvgcode::EGCodeExtrusionRole::InternalInfill},
    {"solid_infill", libvgcode::EGCodeExtrusionRole::SolidInfill},
    {"top_solid_infill", libvgcode::EGCodeExtrusionRole::TopSolidInfill},
    {"ironing", libvgcode::EGCodeExtrusionRole::Ironing},
    {"bridge_infill", libvgcode::EGCodeExtrusionRole::BridgeInfill},
    {"gap_fill", libvgcode::EGCodeExtrusionRole::GapFill},
    {"skirt", libvgcode::EGCodeExtrusionRole::Skirt},
    {"support_material", libvgcode::EGCodeExtrusionRole::SupportMaterial},
    {"support_material_interface", libvgcode::EGCodeExtrusionRole::SupportMaterialInterface},
    {"wipe_tower", libvgcode::EGCodeExtrusionRole::WipeTower},
    {"custom", libvgcode::EGCodeExtrusionRole::Custom},
};
static_assert(std::size(ROLE_NAMES) + 1 == libvgcode::GCODE_EXTRUSION_ROLES_COUNT, "a name for every role but None");

// The roles a hide value names, comma separated ("" or "none": none); false, with the name in `unknown`, at the first
// name that is not a feature type
bool parse_hidden_roles(const std::string &value, std::set<libvgcode::EGCodeExtrusionRole> &roles, std::string &unknown)
{
    roles.clear();
    if (value == "none")
        return true;
    std::istringstream names(value);
    for (std::string name; std::getline(names, name, ',');)
    {
        if (name.empty())
            continue;
        const auto it = std::find_if(std::begin(ROLE_NAMES), std::end(ROLE_NAMES),
                                     [&name](const RoleName &entry) { return name == entry.name; });
        if (it == std::end(ROLE_NAMES))
        {
            unknown = name;
            return false;
        }
        roles.insert(it->role);
    }
    return true;
}

GLCanvas3D *render_canvas()
{
    return wxGetApp().plater() != nullptr ? wxGetApp().plater()->get_current_canvas3D() : nullptr;
}

// The layer slider's thumb positions the range key names for a print of `layers` layers: slider position p shows
// toolpath layers up to p - 1; false without layers
bool range_slider_positions(size_t layers, int &lo_pos, int &hi_pos)
{
    if (layers == 0)
        return false;
    const std::string range = render_value("range", "0,1");
    const size_t comma = range.find(',');
    double lo = std::clamp(std::atof(range.substr(0, comma).c_str()), 0.0, 1.0);
    double hi = comma != std::string::npos ? std::clamp(std::atof(range.substr(comma + 1).c_str()), 0.0, 1.0) : 1.0;
    if (lo > hi)
        std::swap(lo, hi);
    const double last = double(layers - 1);
    lo_pos = 1 + int(std::lround(lo * last));
    hi_pos = 1 + int(std::lround(hi * last));
    return true;
}

// The fractions of the moves slider's span a moves value names, lower first; false unless it is two numbers from 0 to
// 1 separated by a comma
bool parse_moves_fractions(const std::string &value, double &lo, double &hi)
{
    const size_t comma = value.find(',');
    if (comma == std::string::npos)
        return false;
    const auto fraction = [](const std::string &text, double &out)
    {
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        return !text.empty() && (in >> out) && (in >> std::ws).eof() && out >= 0.0 && out <= 1.0;
    };
    if (!fraction(value.substr(0, comma), lo) || !fraction(value.substr(comma + 1), hi))
        return false;
    if (lo > hi)
        std::swap(lo, hi);
    return true;
}

// The Preview that owns the canvas; null for the Prepare view's canvas
Preview *render_preview(GLCanvas3D &canvas)
{
    return dynamic_cast<Preview *>(canvas.get_wxglcanvas_parent());
}

void write_text(const std::string &path, const std::string &text)
{
    boost::nowide::ofstream out(path);
    out << text;
}

// Every wall vertex of the loaded toolpaths with its prefilter neighbour data, in the C locale so the decimal
// separator is always a point
void write_prefilter_csv(const std::string &path, const GCodeViewer &viewer)
{
    const std::vector<float> &offset_x = viewer.get_prefilter_offsets_x();
    const std::vector<float> &offset_y = viewer.get_prefilter_offsets_y();
    const std::vector<uint8_t> &flags = viewer.get_prefilter_flags();
    const size_t count = std::min({flags.size(), offset_x.size(), offset_y.size()});
    boost::nowide::ofstream out(path);
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(5) << "x,y,z,flags,offset_x,offset_y\n";
    for (size_t i = 0; i < count; ++i)
    {
        if ((flags[i] & 1) == 0)
            continue;
        const libvgcode::Vec3 &position = viewer.get_gcode_vertex_at(i).position;
        out << position[0] << ',' << position[1] << ',' << position[2] << ',' << int(flags[i]) << ',' << offset_x[i]
            << ',' << offset_y[i] << '\n';
    }
}

// The visibility probe's class of each pixel as an image: no toolpath black, drawn by the pass grey, dropped by the
// chunk selection or left out of the occlusion draw set blue. Nothing is written for a mask that does not hold width x
// height classes.
void write_probe_png(const std::string &path, const std::vector<uint8_t> &mask, int width, int height)
{
    if (width <= 0 || height <= 0 || mask.size() != size_t(width) * size_t(height))
        return;
    static constexpr uint8_t colours[3][3] = {{0, 0, 0}, {128, 128, 128}, {60, 120, 255}};
    std::vector<uint8_t> rgb(mask.size() * 3);
    const size_t row = size_t(width);
    for (int y = 0; y < height; ++y)
    {
        // GL rows run bottom up, PNG rows top down
        const uint8_t *src = mask.data() + size_t(height - 1 - y) * row;
        uint8_t *dst = rgb.data() + size_t(y) * row * 3;
        for (size_t x = 0; x < row; ++x)
            std::copy_n(colours[std::min<uint8_t>(src[x], 2)], 3, dst + x * 3);
    }
    Luminary::png::write_rgb_to_file(path, size_t(width), size_t(height), rgb.data());
}

// The view, the strip, the render preferences, the layer range, the moves slider (the fractions parsed from the moves
// key), the hidden feature types (parsed from the hide key) and the camera the accumulated keys name
void apply_render_state(const std::set<libvgcode::EGCodeExtrusionRole> &hidden_roles, double moves_lo, double moves_hi)
{
    Plater *plater = wxGetApp().plater();
    AppConfig *config = wxGetApp().app_config;
    const bool preview = render_value("view", "prepare") == "preview";
    plater->select_view_3D(preview ? "Preview" : "3D");
    GLCanvas3D *canvas = render_canvas();
    if (canvas == nullptr)
        return;
    const bool strip_hidden = render_value("strip", "shown") == "hidden";
    if (preview)
    {
        // The Preview's strip is free while the legend, not the sidebar, owns it and the legend is off
        config->set("preview_sidebar", "0");
        canvas->show_legend(!strip_hidden);
    }
    else if (wxGetApp().sidebar().is_collapsed != strip_hidden)
        plater->collapse_sidebar(strip_hidden);
    config->set("canvas_lighting_quality", render_value("lighting", config->get("canvas_lighting_quality")));
    wxGetApp().request_phong_shaders();
    config->set("canvas_ssaa_scale", render_value("ssaa", config->get("canvas_ssaa_scale")));
    // The toolpath prefilter and SSAO toolpath normal keys stay as the config has them until a line names them
    if (const std::string pf = render_value("pf", ""); !pf.empty())
        config->set("toolpath_prefilter", pf);
    if (const std::string aon = render_value("aon", ""); !aon.empty())
        config->set("ao_toolpath_normals", aon);
    SceneSupersampler::s_test_force_offscreen = render_value("offscreen", "0") == "1";
    canvas->get_gcode_viewer().set_sample_shading(render_value("ss", "1") != "0");
    canvas->get_gcode_viewer().set_sealed_bead_culling(render_value("cull", "1") != "0");
    canvas->get_gcode_viewer().set_chunk_culling(render_value("chunkcull", "1") != "0");
    canvas->get_gcode_viewer().set_occlusion_culling(render_value("occl", "1") != "0");
    canvas->get_gcode_viewer().set_occlusion_occluder_cap(
        size_t(std::strtoull(render_value("occl_cap", "0").c_str(), nullptr, 10)));
    // The shadow view's whole-set limit: the viewer's built-in one until a line names it, and for "default"
    if (!s_occl_shadow_all_default)
        s_occl_shadow_all_default = canvas->get_gcode_viewer().get_occlusion_shadow_all_max();
    const std::string shadow_all = render_value("occl_shadow_all", "default");
    canvas->get_gcode_viewer().set_occlusion_shadow_all_max(
        shadow_all == "default" ? *s_occl_shadow_all_default : size_t(std::strtoull(shadow_all.c_str(), nullptr, 10)));
    canvas->get_gcode_viewer().set_occlusion_shadow_merge(render_value("occl_shadow_merge", "1") != "0");
    if (preview && canvas->get_gcode_viewer().has_data())
    {
        // The layer range moves the layer slider's thumbs, as a drag does. The Preview's setter puts its second
        // argument on the lower thumb and its first on the higher.
        int lo_pos = 0;
        int hi_pos = 0;
        if (range_slider_positions(canvas->get_gcode_viewer().get_layers_zs().size(), lo_pos, hi_pos))
            plater->set_preview_layers_slider_values_range(hi_pos, lo_pos);
        // The moves slider's thumbs, as a drag sets them. Every layer range update resets the moves slider to its full
        // span, so each line sets them again after the range; a drag that leaves them in place moves nothing.
        if (Preview *host = render_preview(*canvas))
        {
            int lower = 0;
            int higher = 0;
            int min_pos = 0;
            int max_pos = 0;
            host->test_moves_slider_positions(lower, higher, min_pos, max_pos);
            const double span = double(max_pos - min_pos);
            const int target_lower = min_pos + int(std::lround(moves_lo * span));
            const int target_higher = min_pos + int(std::lround(moves_hi * span));
            if (target_lower != lower || target_higher != higher)
                host->test_set_moves_slider_span(target_lower, target_higher);
        }
        // The feature types toggle as the legend toggles them, after the sliders as a user moves a slider and then
        // clicks the legend; the enabled list is rebuilt by the next render
        canvas->get_gcode_viewer().test_set_hidden_extrusion_roles(hidden_roles);
    }

    canvas->select_view(render_value("cam", "iso"));
    if (render_value("fit", "objects") == "bed")
        canvas->zoom_to_bed();
    else if (preview)
        canvas->zoom_to_gcode();
    else
        canvas->zoom_to_volumes();
    Camera &camera = canvas->get_camera();
    camera.rotate_on_sphere(std::atof(render_value("az", "0").c_str()) * DEGREES,
                            std::atof(render_value("zen", "0").c_str()) * DEGREES, false);
    const double zoom = std::atof(render_value("zoom", "1").c_str());
    if (zoom > 0.0)
        camera.set_zoom(camera.get_zoom() * zoom);
    canvas->set_as_dirty();
}

// slide=<n>: the layer slider's upper thumb moved down one layer at a time n times (no lower than the lower thumb), then
// back up as many times, each move made through the slider as a drag makes it and followed by one frame finished on
// the GPU. The steps' times as sidecar lines: the whole step on average and at its worst, and per step on average its
// parts, which add up to it: the viewer's view ranges, colors, lists, list chunk set, list uploads and structure
// filter, wherever they ran; the host's moves slider update without them; the frame without them.
std::string slide_layers(GLCanvas3D &canvas, int count)
{
    Plater *plater = wxGetApp().plater();
    GCodeViewer &viewer = canvas.get_gcode_viewer();
    std::vector<int> positions;
    int lo_pos = 0;
    int hi_pos = 0;
    if (count > 0 && viewer.has_data() && range_slider_positions(viewer.get_layers_zs().size(), lo_pos, hi_pos))
    {
        const int steps = std::min(count, hi_pos - lo_pos);
        for (int i = 1; i <= steps; ++i)
            positions.push_back(hi_pos - i);
        for (int i = steps - 1; i >= 0; --i)
            positions.push_back(hi_pos - i);
    }
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point from, Clock::time_point to)
    {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    double step_ms = 0.0;
    double step_max_ms = 0.0;
    double frame_ms = 0.0;
    double moves_slider_ms = 0.0;
    double rounds = 0.0;
    libvgcode::ViewUpdateStats parts;
    for (const int position : positions)
    {
        viewer.reset_view_update_stats();
        viewer.reset_layers_range_times();
        viewer.reset_occlusion_bench();
        const Clock::time_point start = Clock::now();
        plater->set_preview_layers_slider_values_range(position, lo_pos);
        const Clock::time_point moved = Clock::now();
        const double moved_viewer_ms = viewer.get_view_update_stats().total_ms();
        canvas.set_as_dirty();
        canvas.render();
        canvas.test_finish_gl();
        const Clock::time_point end = Clock::now();
        const libvgcode::ViewUpdateStats &stats = viewer.get_view_update_stats();
        const double step = ms(start, end);
        step_ms += step;
        step_max_ms = std::max(step_max_ms, step);
        frame_ms += ms(moved, end) - (stats.total_ms() - moved_viewer_ms);
        moves_slider_ms += viewer.get_layers_range_times().moves_slider_ms;
        rounds += double(viewer.get_occlusion_stats().camera_bench.rounds);
        parts.view_range_ms += stats.view_range_ms;
        parts.colors_ms += stats.colors_ms;
        parts.colors_bytes += stats.colors_bytes;
        parts.lists_ms += stats.lists_ms;
        parts.lists_deferred += stats.lists_deferred;
        parts.chunks_ms += stats.chunks_ms;
        parts.upload_ms += stats.upload_ms;
        parts.upload_bytes += stats.upload_bytes;
        parts.filter_ms += stats.filter_ms;
    }
    const double steps = positions.empty() ? 1.0 : double(positions.size());
    std::ostringstream out;
    out << "slide_steps=" << positions.size() << "\nslide_ms_avg=" << step_ms / steps
        << "\nslide_ms_max=" << step_max_ms << "\nslide_view_range_ms=" << parts.view_range_ms / steps
        << "\nslide_colors_ms=" << parts.colors_ms / steps
        << "\nslide_colors_bytes_avg=" << double(parts.colors_bytes) / steps
        << "\nslide_moves_slider_ms=" << moves_slider_ms / steps << "\nslide_lists_ms=" << parts.lists_ms / steps
        << "\nslide_lists_deferred=" << parts.lists_deferred << "\nslide_chunks_ms=" << parts.chunks_ms / steps
        << "\nslide_upload_ms=" << parts.upload_ms / steps
        << "\nslide_upload_bytes_avg=" << double(parts.upload_bytes) / steps
        << "\nslide_filter_ms=" << parts.filter_ms / steps << "\nslide_frame_ms=" << frame_ms / steps
        << "\nslide_occl_rounds_avg=" << rounds / steps << "\n";
    return out.str();
}

void run_render_step(size_t index);

void capture_render_step(size_t index)
{
    GLCanvas3D *canvas = render_canvas();
    if (canvas == nullptr)
        return;
    const RenderStep &step = s_render_steps[index];
    const std::string base = s_render_out + "/" + step.name;
    // A probe line probes the captured frame
    const auto probe_key = step.keys.find("probe");
    const bool probe = probe_key != step.keys.end() && probe_key->second == "1";
    canvas->test_capture_scene(
        [base](const std::vector<uint8_t> &rgb, int width, int height)
        {
            // GL rows run bottom up, PNG rows top down
            std::vector<uint8_t> top_down(rgb.size());
            const size_t row = size_t(width) * 3;
            for (int y = 0; y < height; ++y)
                std::copy_n(rgb.data() + size_t(height - 1 - y) * row, row, top_down.data() + size_t(y) * row);
            Luminary::png::write_rgb_to_file(base + ".png", size_t(width), size_t(height), top_down.data());
        });
    canvas->set_as_dirty();
    if (probe)
        canvas->get_gcode_viewer().request_visibility_probe();
    canvas->render();
    std::string text = RenderDiagnostics::to_key_values(canvas->get_frame_info()) + RenderDiagnostics::counters_text();
    {
        std::ostringstream toolpaths;
        const GCodeViewer &gcode_viewer = canvas->get_gcode_viewer();
        toolpaths << "sample_shading=" << (gcode_viewer.get_sample_shading() ? 1 : 0)
                  << "\nsample_shading_active=" << (gcode_viewer.is_sample_shading_active() ? 1 : 0)
                  << "\nsample_shading_gated=" << (gcode_viewer.is_sample_shading_gated() ? 1 : 0)
                  << "\nsample_shading_reason=" << gcode_viewer.get_sample_shading_reason() << "\n";
        const libvgcode::PrefilterNeighbourStats &neighbours = gcode_viewer.get_prefilter_neighbour_stats();
        toolpaths << "pf_wall_vertices=" << neighbours.wall_vertices
                  << "\npf_found_vertices=" << neighbours.found_vertices
                  << "\npf_excluded_segments=" << neighbours.excluded_segments
                  << "\npf_unmatched_segments=" << neighbours.unmatched_segments
                  << "\npf_search_threads=" << neighbours.search_threads << "\npf_search_ms=" << neighbours.search_ms
                  << "\npf_search_error=" << neighbours.error << "\n";
        const libvgcode::SealedBeadStats &sealed = gcode_viewer.get_sealed_bead_stats();
        toolpaths << "cull_enabled=" << (gcode_viewer.get_sealed_bead_culling() ? 1 : 0)
                  << "\ncull_active=" << (sealed.active ? 1 : 0) << "\ncull_segments_drawn=" << sealed.segments_drawn
                  << "\ncull_segments_total=" << sealed.segments_total << "\ncull_ms=" << sealed.ms
                  << "\ncull_resolution_mm=" << sealed.resolution_mm << "\ncull_threads=" << sealed.threads
                  << "\ncull_error=" << sealed.error << "\n";
        const libvgcode::ViewChunkStats &chunks = gcode_viewer.get_view_chunk_stats();
        toolpaths << "chunkcull_enabled=" << (gcode_viewer.get_chunk_culling() ? 1 : 0)
                  << "\nchunkcull_active=" << (chunks.active ? 1 : 0) << "\nchunks_total=" << chunks.chunks_total
                  << "\nchunks_drawn=" << chunks.chunks_drawn << "\nchunk_segments_drawn=" << chunks.segments_drawn
                  << "\nchunk_shadow_segments_drawn=" << chunks.shadow_segments_drawn
                  << "\nchunk_build_ms=" << chunks.build_ms << "\nchunk_select_ms=" << chunks.select_ms << "\n";
        const libvgcode::ListUploadStats &list_uploads = gcode_viewer.get_list_upload_stats();
        toolpaths << "list_upload_failures=" << list_uploads.failures
                  << "\nlist_upload_retries=" << list_uploads.retries << "\nlist_upload_reason=" << list_uploads.reason
                  << "\n";
        const libvgcode::PrintChunkStats &print_chunks = gcode_viewer.get_print_chunk_stats();
        toolpaths << "pc_valid=" << (print_chunks.valid ? 1 : 0) << "\npc_chunks=" << print_chunks.chunks
                  << "\npc_subcells=" << print_chunks.subcells << "\npc_segments=" << print_chunks.segments
                  << "\npc_bytes=" << print_chunks.bytes << "\npc_build_ms=" << print_chunks.build_ms
                  << "\npc_filter_ms=" << print_chunks.filter_ms
                  << "\npc_candidate_subcells=" << print_chunks.candidate_subcells
                  << "\npc_apply_ms=" << print_chunks.apply_ms
                  << "\npc_applied_subcells=" << print_chunks.applied_subcells << "\n";
        const libvgcode::OcclusionStats &occl = gcode_viewer.get_occlusion_stats();
        toolpaths << "occl_enabled=" << (gcode_viewer.get_occlusion_culling() ? 1 : 0)
                  << "\noccl_cap=" << gcode_viewer.get_occlusion_occluder_cap()
                  << "\noccl_shadow_all_max=" << gcode_viewer.get_occlusion_shadow_all_max()
                  << "\noccl_active=" << (occl.camera.active ? 1 : 0) << "\noccl_reason=" << occl.camera.reason
                  << "\noccl_rounds=" << occl.camera.rounds
                  << "\noccl_points=" << occl.camera.chunks_tested + occl.camera.subcells_tested
                  << "\noccl_visible_subcells=" << occl.camera.subcells_visible
                  << "\noccl_occluder_segments=" << occl.camera.occluder_segments
                  << "\noccl_segments_drawn=" << occl.camera.segments_drawn << "\noccl_ms=" << occl.camera.ms
                  << "\noccl_shadow_active=" << (occl.shadow.active ? 1 : 0)
                  << "\noccl_shadow_all=" << (occl.shadow.all ? 1 : 0) << "\noccl_shadow_reason=" << occl.shadow.reason
                  << "\noccl_shadow_rounds=" << occl.shadow.rounds
                  << "\noccl_shadow_segments_drawn=" << occl.shadow.segments_drawn
                  << "\noccl_shadow_ms=" << occl.shadow.ms
                  << "\noccl_shadow_merge=" << (gcode_viewer.get_occlusion_shadow_merge() ? 1 : 0)
                  << "\noccl_shadow_merged=" << (occl.shadow.merged ? 1 : 0)
                  << "\noccl_shadow_residual_segments=" << occl.shadow.residual_segments
                  << "\noccl_shadow_merge_fallbacks=" << occl.shadow_merge_fallbacks
                  << "\noccl_shadow_merge_reason=" << occl.shadow_merge_reason << "\noccl_steps=" << occl.steps
                  << "\noccl_cached=" << occl.cached << "\noccl_map_fallbacks=" << occl.map_fallbacks
                  << "\noccl_map_fallback_reason=" << occl.map_fallback_reason << "\n";
        const libvgcode::Interval &layers_range = gcode_viewer.get_layers_view_range();
        toolpaths << "layers_range=" << layers_range[0] << ',' << layers_range[1]
                  << "\nlayers_count=" << gcode_viewer.get_layers_zs().size() << "\n";
        // The moves slider's thumbs and last position, read back from it (empty outside the Preview)
        toolpaths << "moves_range=";
        if (Preview *host = render_preview(*canvas))
        {
            int lower = 0;
            int higher = 0;
            int min_pos = 0;
            int max_pos = 0;
            host->test_moves_slider_positions(lower, higher, min_pos, max_pos);
            toolpaths << lower << ',' << higher << ',' << max_pos;
        }
        toolpaths << "\n";
        // The feature types the viewer has hidden, read back from it
        toolpaths << "hidden_roles=";
        bool first_hidden = true;
        for (const RoleName &entry : ROLE_NAMES)
            if (!gcode_viewer.is_extrusion_role_visible(entry.role))
            {
                toolpaths << (first_hidden ? "" : ",") << entry.name;
                first_hidden = false;
            }
        toolpaths << "\n";
        const libvgcode::LoadPhaseStats &load = gcode_viewer.get_load_phase_stats();
        toolpaths << "load_convert_ms=" << load.convert_ms << "\nload_viewer_cpu_ms=" << load.viewer_cpu_ms
                  << "\nload_gl_upload_ms=" << load.gl_upload_ms << "\nload_gl_upload_bytes=" << load.gl_upload_bytes
                  << "\nload_cog_ms=" << load.cog_ms << "\nload_bounds_ms=" << load.bounds_ms
                  << "\nload_gcode_window_ms=" << load.gcode_window_ms << "\nload_total_ms=" << load.total_ms
                  << "\nload_vertices=" << load.vertices << "\n";
        toolpaths << "load_prepare_thread=" << (load.prepared_off_ui ? "slicing" : "ui")
                  << "\nload_prepare_fallback=" << load.prepare_fallback << "\nload_install_ms=" << load.install_ms
                  << "\nload_post_ms=" << load.post_ms << "\nload_first_frame_ms=" << load.first_frame_ms
                  << "\nui_max_stall_ms=" << load.ui_max_stall_ms << "\n";
        float release_free_ms = 0.0f;
        float release_heapmin_ms = 0.0f;
        Preview::last_release_times(release_free_ms, release_heapmin_ms);
        toolpaths << "load_install_swap_ms=" << load.install_swap_ms
                  << "\nload_install_enabled_ms=" << load.install_enabled_ms
                  << "\nload_install_colors_ms=" << load.install_colors_ms
                  << "\nload_view_index_ms=" << load.view_index_ms << "\nload_prepare_view_ms=" << load.prepare_view_ms
                  << "\nload_lists_prepared=" << (load.lists_prepared ? 1 : 0)
                  << "\nload_colors_prepared=" << (load.colors_prepared ? 1 : 0)
                  << "\nload_view_settings_changed=" << load.view_settings_changed
                  << "\nload_post_release_ms=" << load.post_release_ms
                  << "\nload_post_layers_range_ms=" << load.post_layers_range_ms
                  << "\nload_post_moves_slider_ms=" << load.post_moves_slider_ms
                  << "\nload_post_visible_range_ms=" << load.post_visible_range_ms
                  << "\nload_release_free_ms=" << release_free_ms << "\nload_release_heapmin_ms=" << release_heapmin_ms
                  << "\n";
        text += toolpaths.str();
    }
    if (probe)
    {
        const GCodeViewer &gcode_viewer = canvas->get_gcode_viewer();
        const libvgcode::VisibilityProbeStats &stats = gcode_viewer.get_visibility_probe_stats();
        std::ostringstream out;
        out << "probe_ran=" << (stats.ran ? 1 : 0) << "\nprobe_error=" << stats.error << "\nprobe_width=" << stats.width
            << "\nprobe_height=" << stats.height << "\nprobe_list_segments=" << stats.list_segments
            << "\nprobe_drawn_segments=" << stats.drawn_segments
            << "\nprobe_visible_segments=" << stats.visible_segments << "\nprobe_view_segments=" << stats.view_segments
            << "\nprobe_view_chunks=" << stats.view_chunks
            << "\nprobe_view_chunk_segments=" << stats.view_chunk_segments
            << "\nprobe_view_subcells=" << stats.view_subcells << "\nprobe_view_pixels=" << stats.view_pixels
            << "\nprobe_chunk_culled_subcells=" << stats.chunk_culled_subcells
            << "\nprobe_chunk_culled_pixels=" << stats.chunk_culled_pixels << "\nprobe_ms=" << stats.ms
            << "\nprobe_box_chunks_tested=" << stats.box_chunks_tested
            << "\nprobe_box_chunks_visible=" << stats.box_chunks_visible
            << "\nprobe_box_chunks_outside=" << stats.box_chunks_outside
            << "\nprobe_box_chunk_segments=" << stats.box_chunk_segments
            << "\nprobe_box_subcells_tested=" << stats.box_subcells_tested
            << "\nprobe_box_subcells_visible=" << stats.box_subcells_visible
            << "\nprobe_box_segments=" << stats.box_segments
            << "\nprobe_box_exact_subcells_visible=" << stats.box_exact_subcells_visible
            << "\nprobe_box_exact_segments=" << stats.box_exact_segments << "\nprobe_box_misses=" << stats.box_misses
            << "\nprobe_box_ms=" << stats.box_ms << "\nprobe_box4_subcells_visible=" << stats.box4_subcells_visible
            << "\nprobe_box4_segments=" << stats.box4_segments << "\nprobe_box4_misses=" << stats.box4_misses
            << "\nprobe_box8_subcells_visible=" << stats.box8_subcells_visible
            << "\nprobe_box8_segments=" << stats.box8_segments << "\nprobe_box8_misses=" << stats.box8_misses
            << "\nprobe_box4_chunks_visible=" << stats.box4_chunks_visible
            << "\nprobe_box4_chunk_segments=" << stats.box4_chunk_segments
            << "\nprobe_box4_chunk_misses=" << stats.box4_chunk_misses << "\nprobe_box_faces_ms=" << stats.box_faces_ms
            << "\nprobe_pc_chunks_tested=" << stats.pc_chunks_tested
            << "\nprobe_pc_chunks_visible=" << stats.pc_chunks_visible
            << "\nprobe_pc_subcells_tested=" << stats.pc_subcells_tested
            << "\nprobe_pc_subcells_visible=" << stats.pc_subcells_visible
            << "\nprobe_pc_segments=" << stats.pc_segments << "\nprobe_pc_misses=" << stats.pc_misses
            << "\nprobe_pc_enabled_segments=" << stats.pc_enabled_segments << "\nprobe_pc_ms=" << stats.pc_ms
            << "\nprobe_seg_tested=" << stats.seg_tested << "\nprobe_seg_visible=" << stats.seg_visible
            << "\nprobe_seg_misses=" << stats.seg_misses << "\nprobe_seg_owners=" << stats.seg_owners
            << "\nprobe_seg_ms=" << stats.seg_ms << "\nprobe_occl=" << (stats.occl ? 1 : 0)
            << "\nprobe_occl_drawn_segments=" << stats.occl_drawn_segments
            << "\nprobe_occl_missing_subcells=" << stats.occl_missing_subcells
            << "\nprobe_occl_missing_pixels=" << stats.occl_missing_pixels
            << "\nprobe_occl_missing_segments=" << stats.occl_missing_segments
            << "\nprobe_occl_error=" << stats.occl_error << "\nprobe_occl_xcheck_chunks=" << stats.occl_xcheck_chunks
            << "\nprobe_occl_xcheck_chunk_gpu_only=" << stats.occl_xcheck_chunk_gpu_only
            << "\nprobe_occl_xcheck_chunk_cpu_only=" << stats.occl_xcheck_chunk_cpu_only
            << "\nprobe_occl_xcheck_subcells=" << stats.occl_xcheck_subcells
            << "\nprobe_occl_xcheck_gpu_only=" << stats.occl_xcheck_gpu_only
            << "\nprobe_occl_xcheck_cpu_only=" << stats.occl_xcheck_cpu_only
            << "\nprobe_occl_xcheck_error=" << stats.occl_xcheck_error
            << "\nprobe_shadow_ran=" << (stats.shadow_ran ? 1 : 0) << "\nprobe_shadow_error=" << stats.shadow_error
            << "\nprobe_shadow_texels=" << stats.shadow_texels
            << "\nprobe_shadow_missing_texels=" << stats.shadow_missing_texels
            << "\nprobe_shadow_max_gap=" << stats.shadow_max_gap
            << "\nprobe_shadow_segments_drawn=" << stats.shadow_segments_drawn
            << "\nprobe_shadow_list_segments=" << stats.shadow_list_segments << "\nprobe_shadow_ms=" << stats.shadow_ms
            << "\n";
        text += out.str();
        write_probe_png(base + "_probe.png", gcode_viewer.get_visibility_probe_mask(), stats.width, stats.height);
    }
    if (const auto pfdump = step.keys.find("pfdump"); pfdump != step.keys.end() && pfdump->second == "1")
        write_prefilter_csv(base + "_pf.csv", canvas->get_gcode_viewer());

    const auto bench = step.keys.find("bench");
    const int frames = bench != step.keys.end() ? std::atoi(bench->second.c_str()) : 0;
    if (frames > 0)
    {
        // Each frame's time includes the GPU's work: the frame is finished before the clock stops. The occlusion step's
        // timings accumulate over these frames alone.
        canvas->get_gcode_viewer().reset_occlusion_bench();
        std::vector<double> ms;
        ms.reserve(size_t(frames));
        for (int i = 0; i < frames; ++i)
        {
            canvas->get_camera().rotate_on_sphere(0.2 * DEGREES, 0.0, false);
            canvas->set_as_dirty();
            const auto start = std::chrono::steady_clock::now();
            canvas->render();
            canvas->test_finish_gl();
            ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        std::sort(ms.begin(), ms.end());
        std::ostringstream out;
        out << "bench_frames=" << frames << "\nbench_median_ms=" << ms[ms.size() / 2]
            << "\nbench_p95_ms=" << ms[std::min(ms.size() - 1, (ms.size() * 95) / 100)] << "\n";
        // The occlusion step over the bench frames, per step
        const libvgcode::OcclusionStats &occl = canvas->get_gcode_viewer().get_occlusion_stats();
        const auto per_step = [](double total, size_t steps)
        {
            return steps > 0 ? total / double(steps) : 0.0;
        };
        const libvgcode::OcclusionBenchStats &camera = occl.camera_bench;
        const libvgcode::OcclusionBenchStats &shadow = occl.shadow_bench;
        out << "occl_bench_steps=" << camera.steps << "\noccl_bench_cached=" << camera.cached
            << "\noccl_bench_rounds_avg=" << per_step(double(camera.rounds), camera.steps)
            << "\noccl_bench_points_avg=" << per_step(double(camera.points), camera.steps)
            << "\noccl_bench_segments_avg=" << per_step(double(camera.segments), camera.steps)
            << "\noccl_bench_occluders_avg=" << per_step(double(camera.occluders), camera.steps)
            << "\noccl_bench_visible_avg=" << per_step(double(camera.visible), camera.steps)
            << "\noccl_bench_new_segments_avg=" << per_step(double(camera.new_segments), camera.steps)
            << "\noccl_bench_ms_avg=" << per_step(camera.ms, camera.steps)
            << "\noccl_bench_emit_ms=" << per_step(camera.emit_ms, camera.steps)
            << "\noccl_bench_depth_ms=" << per_step(camera.depth_ms, camera.steps)
            << "\noccl_bench_pyramid_ms=" << per_step(camera.pyramid_ms, camera.steps)
            << "\noccl_bench_test_ms=" << per_step(camera.test_ms, camera.steps)
            << "\noccl_bench_readback_ms=" << per_step(camera.readback_ms, camera.steps)
            << "\noccl_bench_list_ms=" << per_step(camera.list_ms, camera.steps)
            << "\noccl_bench_shadow_steps=" << shadow.steps
            << "\noccl_bench_shadow_ms_avg=" << per_step(shadow.ms, shadow.steps)
            << "\noccl_bench_shadow_all=" << shadow.all << "\noccl_bench_shadow_merged=" << shadow.merged
            << "\noccl_bench_shadow_residual_avg=" << per_step(double(shadow.residual_segments), shadow.merged) << "\n";
        text += out.str();

        const auto bench_pass = step.keys.find("bench_pass");
        if (bench_pass != step.keys.end() && bench_pass->second == "1")
        {
            // A second run of as many frames with a GPU finish at each pass boundary, orbiting back over the
            // same views; the finishes add stalls, so the timed total above stays the frame time of record
            RenderPassTimer::clear();
            RenderPassTimer::set_enabled(true);
            for (int i = 0; i < frames; ++i)
            {
                canvas->get_camera().rotate_on_sphere(-0.2 * DEGREES, 0.0, false);
                canvas->set_as_dirty();
                canvas->render();
            }
            RenderPassTimer::set_enabled(false);
            const std::vector<RenderPassTimer::Frame> pass_frames = RenderPassTimer::take_frames();

            // Pass names in the order first seen; a pass absent from a frame counts as 0 in that frame
            std::vector<std::string> names;
            for (const RenderPassTimer::Frame &frame : pass_frames)
                for (const auto &[name, value] : frame)
                    if (std::find(names.begin(), names.end(), name) == names.end())
                        names.push_back(name);
            auto median = [](std::vector<double> values)
            {
                if (values.empty())
                    return 0.0;
                std::sort(values.begin(), values.end());
                return values[values.size() / 2];
            };
            std::ostringstream passes;
            passes << "bench_pass_frames=" << pass_frames.size() << "\n";
            for (const std::string &name : names)
            {
                std::vector<double> values;
                values.reserve(pass_frames.size());
                for (const RenderPassTimer::Frame &frame : pass_frames)
                {
                    double value = 0.0;
                    for (const auto &[frame_name, frame_ms] : frame)
                        if (frame_name == name)
                            value = frame_ms;
                    values.push_back(value);
                }
                passes << "pass_" << name << "_ms=" << median(std::move(values)) << "\n";
            }
            std::vector<double> sums;
            sums.reserve(pass_frames.size());
            for (const RenderPassTimer::Frame &frame : pass_frames)
            {
                double sum = 0.0;
                for (const auto &[name, value] : frame)
                    sum += value;
                sums.push_back(sum);
            }
            passes << "pass_sum_ms=" << median(std::move(sums)) << "\n";
            text += passes.str();
        }
    }
    // The layer slider stepped down and back up after the capture and the bench, timed step by step
    if (const auto slide = step.keys.find("slide"); slide != step.keys.end())
        text += slide_layers(*canvas, std::atoi(slide->second.c_str()));
    write_text(base + ".txt", text);
    after_ms(50, [index]() { run_render_step(index + 1); });
}

void run_render_step(size_t index)
{
    if (index >= s_render_steps.size())
    {
        write_text(s_render_out + "/_done.txt", "steps=" + std::to_string(s_render_steps.size()) + "\n");
        after_ms(500, close_frame);
        return;
    }
    for (const auto &[key, value] : s_render_steps[index].keys)
        if (key != "bench" && key != "bench_pass" && key != "pfdump" && key != "probe" && key != "slide" &&
            key != "glrelease")
            s_render_state[key] = value;
    // A hide value naming anything but feature types ends the run
    std::set<libvgcode::EGCodeExtrusionRole> hidden_roles;
    std::string unknown;
    if (!parse_hidden_roles(render_value("hide", ""), hidden_roles, unknown))
    {
        write_text(s_render_out + "/_error.txt",
                   "line " + s_render_steps[index].name + ": hide names an unknown feature type: " + unknown + "\n");
        close_frame();
        return;
    }
    // A moves value that is not two fractions ends the run
    double moves_lo = 0.0;
    double moves_hi = 1.0;
    const std::string moves = render_value("moves", "0,1");
    if (!parse_moves_fractions(moves, moves_lo, moves_hi))
    {
        write_text(s_render_out + "/_error.txt",
                   "line " + s_render_steps[index].name + ": moves is not two fractions from 0 to 1: " + moves + "\n");
        close_frame();
        return;
    }
    // The line's state change runs with no GL context current, as after the window was deactivated
    if (const auto glrelease = s_render_steps[index].keys.find("glrelease");
        glrelease != s_render_steps[index].keys.end() && glrelease->second == "1")
        if (GLCanvas3D *canvas = render_canvas())
            canvas->release_gl_context();
    apply_render_state(hidden_roles, moves_lo, moves_hi);
    // Frames that settle the step: the legend reframes the scene on the frame after it changes, and a lighting
    // tier's shaders compile at the start of the next frame
    after_ms(300,
             [index]()
             {
                 if (GLCanvas3D *canvas = render_canvas())
                 {
                     canvas->render();
                     canvas->set_as_dirty();
                     canvas->render();
                 }
                 after_ms(300, [index]() { capture_render_step(index); });
             });
}

// How long no slice may be running, with no background update scheduled and no toolpaths, before the slice is taken
// to have ended without toolpaths. reslice() starts a valid slice before it returns, and Plater::is_slicing() holds
// from then until the UI event that takes the result, which loads the Preview in the same handler. A slice that will
// draw leaves this state only for a queued re-slice (one pending event) or the 100 ms background update timer; 5 s
// is fifty times the longer of the two. A slow slice never counts toward it: it is running the whole time.
constexpr int SLICE_IDLE_LIMIT_MS = 5000;
// The whole wait for toolpaths, which catches a slice that never ends. A multi-million segment print slices for
// longer than ten minutes in a debug build.
constexpr int SLICE_WAIT_LIMIT_MS = 60 * 60 * 1000;

// The first line of _error.txt when the slice left no toolpaths: what the active bed's print status tells
std::string no_toolpaths_reason()
{
    const std::string fit = " (does the project fit the selected printer's bed and height?)";
    switch (Luminary::s_print_statuses[Luminary::s_multiple_beds.get_active_bed()])
    {
    case Luminary::PrintStatus::empty:
        return "the slice produced no toolpaths: no object is inside the print volume" + fit;
    case Luminary::PrintStatus::outside:
        return "the slice produced no toolpaths: an object is partly outside the print volume" + fit;
    case Luminary::PrintStatus::invalid:
        return "the slice produced no toolpaths: the print settings are invalid";
    default:
        return "the slice produced no toolpaths (no object inside the print volume, or a slicing error?)";
    }
}

// Polls every 500 ms until the Preview has toolpaths. idle_ms counts the time no slice has been running or
// scheduled; waited_ms the whole wait.
void wait_for_toolpaths(int waited_ms, int idle_ms)
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    GLCanvas3D *canvas = render_canvas();
    if (canvas != nullptr && plater->is_preview_shown() && canvas->get_gcode_viewer().has_data())
    {
        after_ms(1000, []() { run_render_step(0); });
        return;
    }
    if (plater->is_slicing() || plater->is_background_process_update_scheduled())
        idle_ms = 0;
    if (idle_ms >= SLICE_IDLE_LIMIT_MS)
    {
        write_text(s_render_out + "/_error.txt", no_toolpaths_reason() + "\n");
        close_frame();
        return;
    }
    if (waited_ms > SLICE_WAIT_LIMIT_MS)
    {
        write_text(s_render_out + "/_error.txt", "no toolpaths within 60 minutes of the slice\n");
        close_frame();
        return;
    }
    after_ms(500, [waited_ms, idle_ms]() { wait_for_toolpaths(waited_ms + 500, idle_ms + 500); });
}

void start_render_steps()
{
    s_render_steps = read_render_steps(env("PREFLIGHT_RENDER_STEPS"));
    s_render_out = env("PREFLIGHT_RENDER_OUT");
    if (s_render_out.empty())
        s_render_out = ".";
    // A reused output directory must not keep the previous run's ending, which the A/B gate reads as this run's
    boost::system::error_code ignored;
    boost::filesystem::remove(s_render_out + "/_done.txt", ignored);
    boost::filesystem::remove(s_render_out + "/_error.txt", ignored);
    // The viewer drops the per-vertex neighbour data after a load unless kept, and a pfdump line reads it: set before
    // the Preview first loads
    GCodeViewer::s_test_keep_prefilter_neighbours = std::any_of(s_render_steps.begin(), s_render_steps.end(),
                                                                [](const RenderStep &step)
                                                                {
                                                                    auto it = step.keys.find("pfdump");
                                                                    return it != step.keys.end() && it->second == "1";
                                                                });
    boost::filesystem::create_directories(s_render_out);
    const bool preview = std::any_of(s_render_steps.begin(), s_render_steps.end(),
                                     [](const RenderStep &step)
                                     {
                                         auto it = step.keys.find("view");
                                         return it != step.keys.end() && it->second == "preview";
                                     });
    if (!preview || wxGetApp().plater() == nullptr)
    {
        run_render_step(0);
        return;
    }
    wxGetApp().plater()->select_view_3D("Preview");
    wxGetApp().plater()->reslice();
    wait_for_toolpaths(0, 0);
}

// PREFLIGHT_DUMP_SIDEBAR=<dir>: the settings registry of every surface (the three sidebar panels in
// both layouts and both visibility modes, the three Settings pages), so two builds of the settings
// layout can be compared. PREFLIGHT_DUMP_SIDEBAR_SKIP_SIDEBAR and _SKIP_TABS leave out a half;
// PREFLIGHT_DUMP_SIDEBAR_KEEP leaves the window open after the dump.
void dump_registry(GUI_App &app, const std::string &dir)
{
    boost::filesystem::create_directories(dir);
    if (app.plater() != nullptr && std::getenv("PREFLIGHT_DUMP_SIDEBAR_SKIP_SIDEBAR") == nullptr)
        app.sidebar().dump_settings_registry(dir);
    if (std::getenv("PREFLIGHT_DUMP_SIDEBAR_SKIP_TABS") == nullptr)
        for (Tab *tab : app.tabs_list)
        {
            const char *name = tab->type() == Luminary::Preset::TYPE_PRINT      ? "print"
                               : tab->type() == Luminary::Preset::TYPE_FILAMENT ? "filament"
                               : tab->type() == Luminary::Preset::TYPE_PRINTER  ? "printer"
                                                                                : nullptr;
            if (name != nullptr)
                tab->dump_registry(dir + "/tab_" + name + ".txt");
        }
    // The frame closes after the start-up's own deferred work (the splash timer among it) has run
    if (std::getenv("PREFLIGHT_DUMP_SIDEBAR_KEEP") == nullptr)
        after_ms(2000, close_frame);
}

} // namespace

void on_window_up(GUI_App &app)
{
    if (const std::string dir = env("PREFLIGHT_DUMP_SIDEBAR"); !dir.empty())
        app.CallAfter([&app, dir]() { dump_registry(app, dir); });

    // The frame closes as after the registry dump
    if (const std::string path = env("PREFLIGHT_GUI_PROBE"); !path.empty())
        app.CallAfter(
            [&app, path]()
            {
                GuiBudget::run_probe(path, [&app](wxWindow *parent, const wxString &label)
                                     { return app.sidebar().create_probe_group_box(parent, label); });
                after_ms(2000, close_frame);
            });

    // With ",edit" in Edit Visibility mode, so a screenshot shows a settings panel without a click
    if (const std::string tab = env("PREFLIGHT_SIDEBAR_TAB"); !tab.empty())
    {
        const int index = std::atoi(tab.c_str());
        const bool edit = tab.find(",edit") != std::string::npos;
        app.CallAfter(
            [&app, index, edit]()
            {
                if (app.plater() == nullptr)
                    return;
                if (edit)
                    app.sidebar().SetEditVisibilityMode(true);
                app.sidebar().select_sidebar_tab(index);
            });
    }

    // An open delay of 0 opens once the command-line load has returned and the panel's rows are
    // built for the loaded printer (on_loaded), whatever that takes
    if (const std::string spec = env("PREFLIGHT_OPEN_OVERRIDES"); !spec.empty())
    {
        int open_ms = 0, close_ms = 0;
        std::sscanf(spec.c_str(), "%d,%d", &open_ms, &close_ms);
        if (open_ms == 0)
            s_open_after_load = true;
        else
            after_ms(open_ms, open_first_object_overrides);
        if (close_ms > 0)
            after_ms(close_ms, close_frame);
    }

    // A lowered count resets the assignments above it; the trace records extruders.assignments_reset
    // when one was. With ",snapshot" an undo snapshot is taken first, as an edit made before the count
    // change leaves one.
    if (const std::string spec = env("PREFLIGHT_SET_EXTRUDERS"); !spec.empty())
    {
        int count = 0, delay_ms = 0;
        std::sscanf(spec.c_str(), "%d,%d", &count, &delay_ms);
        const bool snapshot = spec.find(",snapshot") != std::string::npos;
        if (count > 0)
            after_ms(delay_ms,
                     [count, snapshot]()
                     {
                         if (wxGetApp().plater() == nullptr)
                             return;
                         if (snapshot)
                             wxGetApp().plater()->take_snapshot(std::string("Test hook: before the extruder count"));
                         wxGetApp().sidebar().set_printer_extruders_count(count);
                     });
    }

    // One undo, as Ctrl+Z does; the trace then records whether the model still assigns an extruder
    // above the printer's count (model.extruders_above, 1 or 0)
    if (const std::string spec = env("PREFLIGHT_UNDO_AT_MS"); !spec.empty())
        after_ms(std::atoi(spec.c_str()),
                 []()
                 {
                     if (wxGetApp().plater() == nullptr)
                         return;
                     wxGetApp().plater()->undo();
                     const size_t count = size_t(std::max(1, wxGetApp().extruders_edited_cnt()));
                     GuiBudget::measure("model.extruders_above",
                                        wxGetApp().plater()->model().has_extruders_above(count) ? 1 : 0);
                     trace_first_object_x("model.first_object_x.after_undo");
                 });

    // A drag's move of the first object, 20 mm along X (model.first_object_x.* before and after)
    if (const std::string spec = env("PREFLIGHT_MOVE_OBJECT_AT_MS"); !spec.empty())
        after_ms(std::atoi(spec.c_str()), move_first_object);

    // A shape added as the bed's context menu adds it (Add Shape > Box by default), which selects it
    if (const std::string spec = env("PREFLIGHT_ADD_SHAPE_AT_MS"); !spec.empty())
    {
        const size_t comma = spec.find(',');
        const std::string shape = comma == std::string::npos ? std::string("Box") : spec.substr(comma + 1);
        after_ms(std::atoi(spec.c_str()),
                 [shape]()
                 {
                     if (wxGetApp().plater() != nullptr)
                         wxGetApp().obj_list()->load_shape_object(shape);
                 });
    }

    // The first object's X in the trace at that time (model.first_object_x.probe), and how many volumes
    // the 3D view's picking would miss: no raycaster, or one whose transform is not the volume's
    // (picking.volumes, picking.volumes_stale)
    if (const std::string spec = env("PREFLIGHT_PROBE_OBJECT_X_AT_MS"); !spec.empty())
        after_ms(std::atoi(spec.c_str()),
                 []()
                 {
                     if (wxGetApp().plater() == nullptr)
                         return;
                     trace_first_object_x("model.first_object_x.probe");
                     GLCanvas3D *canvas = wxGetApp().plater()->canvas3D();
                     const GLVolumeCollection &volumes = canvas->get_volumes();
                     std::vector<bool> covered(volumes.volumes.size(), false);
                     long long stale = 0;
                     if (auto *casters = canvas->get_raycasters_for_picking(SceneRaycaster::EType::Volume))
                         for (const auto &caster : *casters)
                         {
                             const int id = SceneRaycaster::decode_id(SceneRaycaster::EType::Volume, caster->get_id());
                             if (id < 0 || id >= int(volumes.volumes.size()))
                                 continue;
                             covered[id] = true;
                             if (!caster->get_transform().isApprox(volumes.volumes[id]->world_matrix()))
                                 ++stale;
                         }
                     const long long missing = std::count(covered.begin(), covered.end(), false);
                     GuiBudget::measure("picking.volumes", static_cast<long long>(volumes.volumes.size()));
                     GuiBudget::measure("picking.volumes_stale", stale);
                     GuiBudget::measure("picking.volumes_missing", missing);
                 });

    // Selection changes the way the user makes them, one step apart: a click on the first object in
    // the object list, Select All in the list, then in the 3D view the selection cleared and the
    // first object added
    if (const std::string spec = env("PREFLIGHT_SELECT_OBJECTS_AT_MS"); !spec.empty())
        after_ms(std::atoi(spec.c_str()), []() { select_as_clicks(0); });

    // As Help > About opens it (its HTML panel re-measures itself from a size event inside the
    // scrolled panel's update)
    if (const std::string spec = env("PREFLIGHT_OPEN_ABOUT_AT_MS"); !spec.empty())
        after_ms(std::atoi(spec.c_str()), []() { about(); });

#ifdef _WIN32
    if (const std::string spec = env("PREFLIGHT_END_SESSION_AT_MS"); !spec.empty())
        after_ms(std::atoi(spec.c_str()), end_session);
#endif

    // That long after the start-up prebuild begins, the panel rebuilds as a DPI change makes it, so
    // the rebuild lands inside the chunked build (traced as overrides.build.restarted)
    if (const std::string spec = env("PREFLIGHT_REBUILD_OVERRIDES_AT_MS"); !spec.empty() && app.plater() != nullptr)
    {
        const int delay_ms = std::atoi(spec.c_str());
        app.obj_settings()->set_on_prebuild_begin(
            [delay_ms]()
            {
                after_ms(delay_ms,
                         []()
                         {
                             if (wxGetApp().plater() != nullptr)
                                 wxGetApp().obj_settings()->msw_rescale();
                         });
            });
    }
}

void on_loaded(GUI_App &app)
{
    // After the load's own deferred work and the start-up prebuild, so the captured frames are settled
    if (!env("PREFLIGHT_RENDER_STEPS").empty())
        app.CallAfter([]() { when_prebuilt([]() { after_ms(1500, start_render_steps); }); });

    const std::string printer = env("PREFLIGHT_SELECT_PRINTER");
    if (!s_open_after_load && printer.empty())
        return;
    // After the load's own deferred work, once the start-up prebuild is done
    app.CallAfter([printer]() { when_prebuilt([printer]() { switch_printer_then_open(printer); }); });
}

} // namespace GuiTestHooks
} // namespace DSKY
