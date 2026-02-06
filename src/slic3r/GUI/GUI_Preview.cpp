///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, David Kocík @kocikdav, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 André Althaus
///|/ Copyright (c) 2019 John Drake @foxox
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
//#include "stdlib.h"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Layer.hpp"
#include "GUI_Preview.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "GUI_Init.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "OpenGLManager.hpp"
#include "GLCanvas3D.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "DoubleSliderForGcode.hpp"
#include "DoubleSliderForLayers.hpp"
#include "ExtruderSequenceDialog.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "format.hpp"

#include <wx/listbook.h>
#include <wx/notebook.h>
#include <wx/glcanvas.h> // IWYU pragma: keep
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/combo.h>
#include <wx/combobox.h>
#include <wx/checkbox.h>
#include <wx/colordlg.h>

// this include must follow the wxWidgets ones or it won't compile on Windows -> see http://trac.wxwidgets.org/ticket/2421
#include "libslic3r/Print.hpp"
// #include "libslic3r/SLAPrint.hpp"
#include "NotificationManager.hpp"
#include "libslic3r/MultipleBeds.hpp"

#ifdef _WIN32
#include "BitmapComboBox.hpp"
#endif

namespace Slic3r
{
namespace GUI
{

View3D::View3D(wxWindow *parent, Bed3D &bed, Model *model, DynamicPrintConfig *config,
               BackgroundSlicingProcess *process)
    : m_canvas_widget(nullptr), m_canvas(nullptr)
{
    init(parent, bed, model, config, process);
}

View3D::~View3D()
{
    // GLCanvas3D is not a wxWindow, so we must delete it manually
    delete m_canvas;
    m_canvas = nullptr;

    // m_canvas_widget is a child window of View3D - do NOT delete manually
    // wxWidgets will auto-delete it via DestroyChildren() in base class destructor
    m_canvas_widget = nullptr;
}

bool View3D::init(wxWindow *parent, Bed3D &bed, Model *model, DynamicPrintConfig *config,
                  BackgroundSlicingProcess *process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    const GUI_InitParams *const init_params = wxGetApp().init_params;
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this,
                                                       (init_params != nullptr) ? init_params->opengl_aa : false);
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));

    m_canvas->allow_multisample(OpenGLManager::can_multisample());

    m_canvas->enable_picking(true);
    m_canvas->get_selection().set_mode(Selection::Instance);
    m_canvas->enable_moving(true);
    // XXX: more config from 3D.pm
    m_canvas->set_model(model);
    m_canvas->set_process(process);
    m_canvas->set_config(config);
    m_canvas->enable_gizmos(true);
    m_canvas->enable_selection(true);
    m_canvas->enable_main_toolbar(true);
    m_canvas->enable_undoredo_toolbar(true);
    m_canvas->enable_labels(true);
    m_canvas->enable_slope(true);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    return true;
}

void View3D::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void View3D::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void View3D::select_view(const std::string &direction)
{
    if (m_canvas != nullptr)
        m_canvas->select_view(direction);
}

void View3D::select_all()
{
    if (m_canvas != nullptr)
        m_canvas->select_all();
}

void View3D::deselect_all()
{
    if (m_canvas != nullptr)
        m_canvas->deselect_all();
}

void View3D::delete_selected()
{
    if (m_canvas != nullptr)
        m_canvas->delete_selected();
}

void View3D::mirror_selection(Axis axis)
{
    if (m_canvas != nullptr)
        m_canvas->mirror_selection(axis);
}

bool View3D::is_layers_editing_enabled() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_enabled() : false;
}

bool View3D::is_layers_editing_allowed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_allowed() : false;
}

void View3D::enable_layers_editing(bool enable)
{
    if (m_canvas != nullptr)
        m_canvas->enable_layers_editing(enable);
}

bool View3D::is_dragging() const
{
    return (m_canvas != nullptr) ? m_canvas->is_dragging() : false;
}

bool View3D::is_reload_delayed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_reload_delayed() : false;
}

void View3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas != nullptr)
        m_canvas->reload_scene(refresh_immediately, force_full_scene_refresh);
}

void View3D::render()
{
    if (m_canvas != nullptr)
        //m_canvas->render();
        m_canvas->set_as_dirty();
}

Preview::Preview(wxWindow *parent, Bed3D &bed, Model *model, DynamicPrintConfig *config,
                 BackgroundSlicingProcess *process, std::vector<GCodeProcessorResult> *gcode_results,
                 std::function<void()> schedule_background_process_func)
    : m_config(config)
    , m_process(process)
    , m_gcode_results(gcode_results)
    , m_schedule_background_process(schedule_background_process_func)
{
    if (init(parent, bed, model))
        load_print();
}

void Preview::set_layers_slider_values_range(int bottom, int top)
{
    m_layers_slider->SetSelectionSpan(std::min(top, m_layers_slider->GetMaxPos()),
                                      std::max(bottom, m_layers_slider->GetMinPos()));
}

GCodeProcessorResult *Preview::active_gcode_result()
{
    return &(*m_gcode_results)[s_multiple_beds.get_active_bed()];
}

bool Preview::init(wxWindow *parent, Bed3D &bed, Model *model)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    // to match the background of the sliders
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
#else
    SetBackgroundColour(GetParent()->GetBackgroundColour());
#endif // _WIN32

    const GUI_InitParams *const init_params = wxGetApp().init_params;
    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this,
                                                       (init_params != nullptr) ? init_params->opengl_aa : false);
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget, bed);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));
    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    m_canvas->set_config(m_config);
    m_canvas->set_model(model);
    m_canvas->set_process(m_process);
    m_canvas->show_legend(true);
    m_canvas->enable_dynamic_background(true);

    create_sliders();

    m_left_sizer = new wxBoxSizer(wxVERTICAL);
    m_left_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxHORIZONTAL);
    main_sizer->Add(m_left_sizer, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    bind_event_handlers();

    return true;
}

Preview::~Preview()
{
    unbind_event_handlers();

    // GLCanvas3D is not a wxWindow, so we must delete it manually
    if (m_canvas != nullptr)
    {
        delete m_canvas;
        m_canvas = nullptr;
    }

    // m_canvas_widget is a child window of Preview - do NOT delete manually
    // wxWidgets will auto-delete it via DestroyChildren() in base class destructor
    m_canvas_widget = nullptr;
}

void Preview::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void Preview::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void Preview::select_view(const std::string &direction)
{
    m_canvas->select_view(direction);
}

void Preview::set_drop_target(wxDropTarget *target)
{
    if (target != nullptr)
        SetDropTarget(target);
}

void Preview::load_print(bool keep_z_range)
{
    PrinterTechnology tech = m_process->current_printer_technology();
    if (tech == ptFFF)
        load_print_as_fff(keep_z_range);
    else if (tech == ptSLA)
        load_print_as_sla();

    Layout();
}

void Preview::reload_print()
{
    if (!IsShown())
        return;

    m_loaded = false;
    load_print();
    m_layers_slider->seq_top_layer_only(wxGetApp().app_config->get_bool("seq_top_layer_only"));
}

void Preview::msw_rescale()
{
    m_layers_slider->SetEmUnit(wxGetApp().em_unit());
    m_moves_slider->SetEmUnit(wxGetApp().em_unit());
    // rescale warning legend on the canvas
    get_canvas3d()->msw_rescale();

    // rescale legend
    reload_print();
}

void Preview::render_sliders(GLCanvas3D &canvas)
{
    const Size cnv_size = canvas.get_canvas_size();
    const int canvas_width = cnv_size.get_width();
    const int canvas_height = cnv_size.get_height();
    const float extra_scale = cnv_size.get_scale_factor();

    // collapse toolbar not used, no offset needed for layers slider
    if (m_layers_slider)
    {
        // Use full canvas height now that view toolbar is hidden
        m_layers_slider->Render(canvas_width, canvas_height, extra_scale, 0.f);
    }
    if (m_moves_slider)
        m_moves_slider->Render(canvas_width, canvas_height, extra_scale);
}

float Preview::get_moves_slider_height() const
{
    if (!s_multiple_beds.is_autoslicing() && m_moves_slider && m_moves_slider->IsShown())
        return m_moves_slider->GetHeight();
    return 0.0f;
}

float Preview::get_layers_slider_width(bool disregard_visibility) const
{
    if (!s_multiple_beds.is_autoslicing() && m_layers_slider && (m_layers_slider->IsShown() || disregard_visibility))
        return m_layers_slider->GetWidth();
    return 0.0f;
}

void Preview::bind_event_handlers()
{
    Bind(wxEVT_SIZE, &Preview::on_size, this);
}

void Preview::unbind_event_handlers()
{
    Unbind(wxEVT_SIZE, &Preview::on_size, this);
}

void Preview::hide_layers_slider()
{
    m_layers_slider->Hide();
}

void Preview::on_size(wxSizeEvent &evt)
{
    evt.Skip();
    m_layers_slider->force_ruler_update();
    Refresh();
}

/* To avoid get an empty string from wxTextEntryDialog
 * Let disable OK button, if TextCtrl is empty
 * */
static void upgrade_text_entry_dialog(wxTextEntryDialog *dlg, double min = -1.0, double max = -1.0)
{
    GUI::wxGetApp().UpdateDlgDarkUI(dlg);

    // detect TextCtrl and OK button
    wxWindowList &dlg_items = dlg->GetChildren();
    for (auto item : dlg_items)
    {
        if (wxTextCtrl *textctrl = dynamic_cast<wxTextCtrl *>(item))
        {
            textctrl->SetInsertionPointEnd();

            wxButton *btn_OK = static_cast<wxButton *>(dlg->FindWindowById(wxID_OK));
            btn_OK->Bind(
                wxEVT_UPDATE_UI, [textctrl](wxUpdateUIEvent &evt) { evt.Enable(!textctrl->IsEmpty()); },
                btn_OK->GetId());

            break;
        }
    }
}

void Preview::create_sliders()
{
    // Layers Slider

    m_layers_slider = std::make_unique<DoubleSlider::DSForLayers>(0, 0, 0, 100, wxGetApp().is_editor());
    m_layers_slider->SetEmUnit(wxGetApp().em_unit());
    m_layers_slider->set_imgui_wrapper(wxGetApp().imgui());
    m_layers_slider->show_estimated_times(wxGetApp().app_config->get_bool("show_estimated_times_in_dbl_slider"));
    m_layers_slider->seq_top_layer_only(wxGetApp().app_config->get_bool("seq_top_layer_only"));
    // Enable ruler and ruler background by default (true if not set in config)
    bool show_ruler = !wxGetApp().app_config->has("show_ruler_in_dbl_slider") ||
                      wxGetApp().app_config->get_bool("show_ruler_in_dbl_slider");
    bool show_ruler_bg = !wxGetApp().app_config->has("show_ruler_bg_in_dbl_slider") ||
                         wxGetApp().app_config->get_bool("show_ruler_bg_in_dbl_slider");
    m_layers_slider->show_ruler(show_ruler, show_ruler_bg);

    m_layers_slider->SetDrawMode(wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA,
                                 wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_bool(
                                     "complete_objects"));

    m_layers_slider->set_callback_on_thumb_move([this]() -> void { Preview::on_layers_slider_scroll_changed(); });

    m_layers_slider->set_callback_on_change_app_config(
        [this](const std::string &key, const std::string &val) -> void
        {
            wxGetApp().app_config->set(key, val);
            if (key == "seq_top_layer_only")
                reload_print();
        });

    if (wxGetApp().is_editor())
    {
        m_layers_slider->set_callback_on_ticks_changed(
            [this]() -> void
            {
                Model &model = wxGetApp().plater()->model();
                model.custom_gcode_per_print_z() = m_layers_slider->GetTicksValues();
                m_schedule_background_process();

                m_keep_current_preview_type = false;
                reload_print();
            });

        m_layers_slider->set_callback_on_check_gcode(
            [this](CustomGCode::Type type) -> void
            {
                if (type == ColorChange && m_layers_slider->gcode(ColorChange).empty())
                    GUI::wxGetApp().plater()->get_notification_manager()->push_notification(
                        GUI::NotificationType::EmptyColorChangeCode);
            });

        m_layers_slider->set_callback_on_empty_auto_color_change(
            []() -> void
            {
                GUI::wxGetApp().plater()->get_notification_manager()->push_notification(
                    GUI::NotificationType::EmptyAutoColorChange);
            });

        m_layers_slider->set_callback_on_get_extruder_colors(
            []() -> std::vector<std::string>
            { return wxGetApp().plater()->get_extruder_color_strings_from_plater_config(); });

        m_layers_slider->set_callback_on_get_print([]() -> const Print &
                                                   { return GUI::wxGetApp().plater()->active_fff_print(); });

        m_layers_slider->set_callback_on_get_custom_code(
            [](const std::string &code_in, double height) -> std::string
            {
                wxString msg_text = _L("Enter custom G-code used on current layer") + ":";
                wxString msg_header = format_wxstr(_L("Custom G-code on current layer (%1% mm)."), height);

                // get custom gcode
                wxTextEntryDialog dlg(nullptr, msg_text, msg_header, code_in, wxTextEntryDialogStyle | wxTE_MULTILINE);
                upgrade_text_entry_dialog(&dlg);

                bool valid = true;
                std::string value;
                do
                {
                    if (dlg.ShowModal() != wxID_OK)
                        return "";

                    value = into_u8(dlg.GetValue());
                    valid = Tab::validate_custom_gcode("Custom G-code", value);
                } while (!valid);
                return value;
            });

        m_layers_slider->set_callback_on_get_pause_print_msg(
            [](const std::string &msg_in, double height) -> std::string
            {
                wxString msg_text = _L("Enter short message shown on Printer display when a print is paused") + ":";
                wxString msg_header = format_wxstr(_L("Message for pause print on current layer (%1% mm)."), height);

                // get custom gcode
                wxTextEntryDialog dlg(nullptr, msg_text, msg_header, from_u8(msg_in), wxTextEntryDialogStyle);
                upgrade_text_entry_dialog(&dlg);

                if (dlg.ShowModal() != wxID_OK || dlg.GetValue().IsEmpty())
                    return "";

                return into_u8(dlg.GetValue());
            });

        m_layers_slider->set_callback_on_get_new_color(
            [](const std::string &color) -> std::string
            {
                wxColour clr(color);
                if (!clr.IsOk())
                    clr = wxColour(0, 0, 0); // Don't set alfa to transparence

                auto data = new wxColourData();
                data->SetChooseFull(1);
                data->SetColour(clr);

                wxColourDialog dialog(GUI::wxGetApp().GetTopWindow(), data);
                dialog.CenterOnParent();
                if (dialog.ShowModal() == wxID_OK)
                    return dialog.GetColourData().GetColour().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
                return "";
            });

        m_layers_slider->set_callback_on_show_info_msg(
            [this](const std::string &message, int btns_flag) -> int
            {
                GUI::MessageDialog msg(this, from_u8(message), _L("Notice"), btns_flag);
                int ret = msg.ShowModal();
                return ret == wxID_YES ? wxYES : ret == wxID_NO ? wxNO : ret == wxID_CANCEL ? wxCANCEL : -1;
            });

        m_layers_slider->set_callback_on_show_warning_msg(
            [this](const std::string &message, int btns_flag) -> int
            {
                GUI::WarningDialog msg(this, from_u8(message), _L("Warning"), btns_flag);
                int ret = msg.ShowModal();
                return ret == wxID_YES ? wxYES : ret == wxID_NO ? wxNO : ret == wxID_CANCEL ? wxCANCEL : -1;
            });

        m_layers_slider->set_callback_on_get_extruders_cnt([]() -> int
                                                           { return GUI::wxGetApp().extruders_edited_cnt(); });

        m_layers_slider->set_callback_on_get_extruders_sequence(
            [](DoubleSlider::ExtrudersSequence &extruders_sequence) -> bool
            {
                GUI::ExtruderSequenceDialog dlg(extruders_sequence);
                if (dlg.ShowModal() != wxID_OK)
                    return false;
                extruders_sequence = dlg.GetValue();
                return true;
            });
    }

    // Move Gcode Slider

    m_moves_slider = std::make_unique<DoubleSlider::DSForGcode>(0, 0, 0, 100);
    m_moves_slider->SetEmUnit(wxGetApp().em_unit());

    m_moves_slider->set_callback_on_thumb_move([this]() -> void { on_moves_slider_scroll_changed(); });

    // m_canvas_widget
    m_canvas_widget->Bind(wxEVT_KEY_DOWN, &Preview::update_sliders_from_canvas, this);
    m_canvas_widget->Bind(EVT_GLCANVAS_SLIDERS_MANIPULATION, &Preview::update_sliders_from_canvas, this);
    m_canvas_widget->Bind(wxEVT_KEY_UP, &Preview::on_key_up, this);
    m_arrow_repeat_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &Preview::on_arrow_repeat_timer, this);

    // Hide sliders from the very begibing. Visibility will be set later
    m_layers_slider->Hide();
    m_moves_slider->Hide();
}

// Find an index of a value in a sorted vector, which is in <z-eps, z+eps>.
// Returns -1 if there is no such member.
static int find_close_layer_idx(const std::vector<double> &zs, double &z, double eps)
{
    if (zs.empty())
        return -1;
    auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
    if (it_h == zs.end())
    {
        auto it_l = it_h;
        --it_l;
        if (z - *it_l < eps)
            return int(zs.size() - 1);
    }
    else if (it_h == zs.begin())
    {
        if (*it_h - z < eps)
            return 0;
    }
    else
    {
        auto it_l = it_h;
        --it_l;
        double dist_l = z - *it_l;
        double dist_h = *it_h - z;
        if (std::min(dist_l, dist_h) < eps)
        {
            return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin());
        }
    }
    return -1;
}

void Preview::check_layers_slider_values(std::vector<CustomGCode::Item> &ticks_from_model,
                                         const std::vector<double> &layers_z)
{
    // All ticks that would end up outside the slider range should be erased.
    // TODO: this should be placed into more appropriate part of code,
    // this function is e.g. not called when the last object is deleted
    unsigned int old_size = ticks_from_model.size();
    ticks_from_model.erase(std::remove_if(ticks_from_model.begin(), ticks_from_model.end(),
                                          [layers_z](CustomGCode::Item val)
                                          {
                                              auto it = std::lower_bound(layers_z.begin(), layers_z.end(),
                                                                         val.print_z - CustomGCode::epsilon());
                                              return it == layers_z.end();
                                          }),
                           ticks_from_model.end());
    if (ticks_from_model.size() != old_size)
        m_schedule_background_process();
}

void Preview::update_layers_slider(const std::vector<double> &layers_z, bool keep_z_range)
{
    // Save the initial slider span.
    double z_low = m_layers_slider->GetLowerValue();
    double z_high = m_layers_slider->GetHigherValue();
    bool was_empty = m_layers_slider->GetMaxPos() == 0;

    bool force_sliders_full_range = was_empty;
    if (!keep_z_range)
    {
        bool span_changed = layers_z.empty() || std::abs(layers_z.back() - m_layers_slider->GetMaxValue()) >
                                                    CustomGCode::epsilon() /*1e-6*/;
        force_sliders_full_range |= span_changed;
    }
    bool snap_to_min = force_sliders_full_range || m_layers_slider->IsLowerAtMin();
    bool snap_to_max = force_sliders_full_range || m_layers_slider->IsHigherAtMax();

    // Detect and set manipulation mode for double slider
    update_layers_slider_mode();

    Plater *plater = wxGetApp().plater();
    CustomGCode::Info ticks_info_from_model;
    if (wxGetApp().is_editor())
        ticks_info_from_model = plater->model().custom_gcode_per_print_z();
    else
    {
        ticks_info_from_model.mode = CustomGCode::Mode::SingleExtruder;
        ticks_info_from_model.gcodes = active_gcode_result()->custom_gcode_per_print_z;
    }
    check_layers_slider_values(ticks_info_from_model.gcodes, layers_z);

    //first of all update extruder colors to avoid crash, when we are switching printer preset from MM to SM
    m_layers_slider->SetExtruderColors(plater->get_extruder_color_strings_from_plater_config(
        wxGetApp().is_editor() ? nullptr : active_gcode_result()));
    m_layers_slider->SetSliderValues(layers_z);
    m_layers_slider->force_ruler_update();
    assert(m_layers_slider->GetMinPos() == 0);

    m_layers_slider->Freeze();

    // With Layer 0 added, we need positions 0 to layers_z.size() (not size()-1)
    m_layers_slider->SetMaxPos(layers_z.empty() ? 0 : static_cast<int>(layers_z.size()));

    int idx_low = 0;
    int idx_high = m_layers_slider->GetMaxPos();
    if (!layers_z.empty())
    {
        if (!snap_to_min)
        {
            int idx_new = find_close_layer_idx(layers_z, z_low, CustomGCode::epsilon() /*1e-6*/);
            if (idx_new != -1)
                idx_low = idx_new;
        }
        if (!snap_to_max)
        {
            int idx_new = find_close_layer_idx(layers_z, z_high, CustomGCode::epsilon() /*1e-6*/);
            if (idx_new != -1)
                idx_high = idx_new;
        }
    }
    m_layers_slider->SetSelectionSpan(idx_low, idx_high);
    m_layers_slider->SetTicksValues(ticks_info_from_model);

    bool sequential_print = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_bool("complete_objects");
    m_layers_slider->SetDrawMode(false, sequential_print);
    m_layers_slider->SetLayersTimes(m_canvas->get_gcode_layers_times_cache(),
                                    active_gcode_result()->print_statistics.modes.front().time);

    m_layers_slider->Thaw();

    // check if ticks_info_from_model contains ColorChange g-code
    bool color_change_already_exists = false;
    for (const CustomGCode::Item &gcode : ticks_info_from_model.gcodes)
        if (gcode.type == CustomGCode::Type::ColorChange)
        {
            color_change_already_exists = true;
            break;
        }

    auto get_print_obj_idxs = []() -> std::string
    {
        const Print &print = GUI::wxGetApp().plater()->active_fff_print();
        std::string idxs;
        for (auto object : print.objects())
            idxs += std::to_string(object->id().id) + "_";
        return idxs;
    };

    // Suggest the auto color change, if model looks like sign
    if (!color_change_already_exists && wxGetApp().app_config->get_bool("allow_auto_color_change") &&
        m_layers_slider->is_new_print(get_print_obj_idxs()))
    {
        const Print &print = wxGetApp().plater()->active_fff_print();

        //bool is_possible_auto_color_change = false;
        for (auto object : print.objects())
        {
            double object_x = double(object->size().x());
            double object_y = double(object->size().y());

            // if it's sign, than object have not to be a too height
            double height = object->height();
            coord_t longer_side = std::max(object_x, object_y);
            auto num_layers = int(object->layers().size());
            if (height / longer_side > 0.3 || num_layers < 2)
                continue;

            const ExPolygons &bottom = object->get_layer(0)->lslices;
            double bottom_area = area(bottom);

            // at least 25% of object's height have to be a solid
            int i, min_solid_height = int(0.25 * num_layers);
            for (i = 1; i <= min_solid_height; ++i)
            {
                double cur_area = area(object->get_layer(i)->lslices);
                if (!DoubleSlider::equivalent_areas(bottom_area, cur_area))
                {
                    // but due to the elephant foot compensation, the first layer may be slightly smaller than the others
                    if (i == 1 && fabs(cur_area - bottom_area) / bottom_area < 0.1)
                    {
                        // So, let process this case and use second layer as a bottom
                        bottom_area = cur_area;
                        continue;
                    }
                    break;
                }
            }
            if (i < min_solid_height)
                continue;

            if (DoubleSlider::check_color_change(
                    object, i, num_layers, true,
                    [this, object](const Layer *)
                    {
                        NotificationManager *notif_mngr = wxGetApp().plater()->get_notification_manager();
                        notif_mngr->push_notification(
                            NotificationType::SignDetected,
                            NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
                            _u8L("NOTE:") + "\n" +
                                format(_u8L("Sliced object \"%1%\" looks like a logo or a sign"),
                                       object->model_object()->name) +
                                "\n",
                            _u8L("Apply color change automatically"),
                            [this](wxEvtHandler *)
                            {
                                m_layers_slider->auto_color_change();
                                return true;
                            });

                        notif_mngr->apply_in_preview();
                        return true;
                    }))
                // first object with color chnages is found
                break;
        }
    }
    m_layers_slider->Show();
}

void Preview::update_layers_slider_mode()
{
    //    true  -> single-extruder printer profile OR
    //             multi-extruder printer profile , but whole model is printed by only one extruder
    //    false -> multi-extruder printer profile , and model is printed by several extruders
    bool one_extruder_printed_model = true;

    // extruder used for whole model for multi-extruder printer profile
    int only_extruder = -1;

    if (wxGetApp().extruders_edited_cnt() > 1)
    {
        const ModelObjectPtrs &objects = wxGetApp().plater()->model().objects;

        // check if whole model uses just only one extruder
        if (!objects.empty())
        {
            const int extruder = objects[0]->config.has("extruder") ? objects[0]->config.option("extruder")->getInt()
                                                                    : 0;

            auto is_one_extruder_printed_model = [objects, extruder]()
            {
                for (ModelObject *object : objects)
                {
                    if (object->config.has("extruder") && object->config.option("extruder")->getInt() != extruder)
                        return false;

                    for (ModelVolume *volume : object->volumes)
                        if ((volume->config.has("extruder") &&
                             volume->config.option("extruder")->getInt() != 0 && // extruder isn't default
                             volume->config.option("extruder")->getInt() != extruder) ||
                            !volume->mm_segmentation_facets.empty())
                            return false;

                    for (const auto &range : object->layer_config_ranges)
                        if (range.second.has("extruder") &&
                            range.second.option("extruder")->getInt() != 0 && // extruder isn't default
                            range.second.option("extruder")->getInt() != extruder)
                            return false;
                }
                return true;
            };

            if (is_one_extruder_printed_model())
                only_extruder = extruder;
            else
                one_extruder_printed_model = false;
        }
    }

    m_layers_slider->SetModeAndOnlyExtruder(one_extruder_printed_model, only_extruder);
}

void Preview::reset_layers_slider()
{
    m_layers_slider->SetSelectionSpan(0, 0);
}

void Preview::update_sliders_from_canvas(wxKeyEvent &event)
{
    const auto key = event.GetKeyCode();

    const bool can_edit = wxGetApp().is_editor();

    if (can_edit && (key == WXK_NUMPAD_ADD || key == '+'))
        m_layers_slider->add_current_tick();
    else if (can_edit && (key == WXK_NUMPAD_SUBTRACT || key == WXK_DELETE || key == WXK_BACK || key == '-'))
        m_layers_slider->delete_current_tick();
    else if (key == 'G' || key == 'g')
        m_layers_slider->jump_to_value();
    else if (key == WXK_LEFT || key == WXK_RIGHT || key == WXK_UP || key == WXK_DOWN)
    {
        // Track which arrow is held and when, for timer coordination
        m_held_arrow_key = key;
        m_last_arrow_keydown_time = wxGetLocalTimeMillis().GetValue();

        int delta = 1;
        if (wxGetKeyState(WXK_CONTROL))
            delta *= 2;
        if (wxGetKeyState(WXK_SHIFT))
            delta *= 4;

        // event.ShiftDown() checks the event's shift flag (set by gcode scroll)
        // wxGetKeyState(WXK_SHIFT) checks actual keyboard (for acceleration)
        bool from_gcode_scroll = event.ShiftDown() && !wxGetKeyState(WXK_SHIFT);
        move_slider_by_key(key, delta, from_gcode_scroll);
    }
    // When Ctrl/Shift is pressed while an arrow key is already held, continue movement
    // with the new accelerator value
    else if (key == WXK_CONTROL || key == WXK_SHIFT ||
             key == WXK_RAW_CONTROL || // For platforms where Ctrl and Raw Ctrl differ
             key == WXK_NUMPAD_LEFT || key == WXK_NUMPAD_RIGHT || key == WXK_NUMPAD_UP || key == WXK_NUMPAD_DOWN)
    {
        // Check if an arrow key is currently held
        int arrow_key = 0;
        if (wxGetKeyState(WXK_LEFT) || wxGetKeyState(WXK_NUMPAD_LEFT))
            arrow_key = WXK_LEFT;
        else if (wxGetKeyState(WXK_RIGHT) || wxGetKeyState(WXK_NUMPAD_RIGHT))
            arrow_key = WXK_RIGHT;
        else if (wxGetKeyState(WXK_UP) || wxGetKeyState(WXK_NUMPAD_UP))
            arrow_key = WXK_UP;
        else if (wxGetKeyState(WXK_DOWN) || wxGetKeyState(WXK_NUMPAD_DOWN))
            arrow_key = WXK_DOWN;

        if (arrow_key != 0)
        {
            int delta = 1;
            if (wxGetKeyState(WXK_CONTROL))
                delta *= 2;
            if (wxGetKeyState(WXK_SHIFT))
                delta *= 4;

            move_slider_by_key(arrow_key, delta);
        }
        else
        {
            event.Skip();
            return;
        }
    }

    else if (event.HasModifiers())
    {
        event.Skip();
        return;
    }

    else if (key == 'S' || key == 'W')
    {
        const int new_pos = key == 'W' ? m_layers_slider->GetHigherPos() + 1 : m_layers_slider->GetHigherPos() - 1;
        m_layers_slider->SetHigherPos(new_pos);
    }
    else if (key == 'A' || key == 'D')
    {
        const int new_pos = key == 'D' ? m_moves_slider->GetHigherPos() + 1 : m_moves_slider->GetHigherPos() - 1;
        m_moves_slider->SetHigherPos(new_pos);
    }
    else if (key == 'X')
        m_layers_slider->ChangeOneLayerLock();
    else
        event.Skip();
}

void Preview::on_key_up(wxKeyEvent &event)
{
    const auto key = event.GetKeyCode();

    // When an arrow key is released, stop repeating
    if (key == WXK_LEFT || key == WXK_RIGHT || key == WXK_UP || key == WXK_DOWN || key == WXK_NUMPAD_LEFT ||
        key == WXK_NUMPAD_RIGHT || key == WXK_NUMPAD_UP || key == WXK_NUMPAD_DOWN)
    {
        m_held_arrow_key = 0;
    }
    // When Ctrl or Shift is released while an arrow key is still held,
    // start repeating navigation at the new speed using CallAfter loop
    else if (key == WXK_CONTROL || key == WXK_SHIFT || key == WXK_RAW_CONTROL)
    {
        // When modifier is released while arrow is held, start timer to take over
        // (OS key repeat may have stopped due to modifier key events)
        int arrow_key = 0;
        if (wxGetKeyState(WXK_LEFT) || wxGetKeyState(WXK_NUMPAD_LEFT))
            arrow_key = WXK_LEFT;
        else if (wxGetKeyState(WXK_RIGHT) || wxGetKeyState(WXK_NUMPAD_RIGHT))
            arrow_key = WXK_RIGHT;
        else if (wxGetKeyState(WXK_UP) || wxGetKeyState(WXK_NUMPAD_UP))
            arrow_key = WXK_UP;
        else if (wxGetKeyState(WXK_DOWN) || wxGetKeyState(WXK_NUMPAD_DOWN))
            arrow_key = WXK_DOWN;

        if (arrow_key != 0)
        {
            m_held_arrow_key = arrow_key;
            // Start timer if not already running
            if (!m_arrow_repeat_timer.IsRunning())
            {
                m_arrow_repeat_timer.StartOnce(30);
            }
        }
    }

    event.Skip();
}

void Preview::do_arrow_repeat()
{
    // Check if we should still be repeating
    if (m_held_arrow_key == 0)
        return;

    // Check if the arrow key is still held
    bool still_held = false;
    if (m_held_arrow_key == WXK_LEFT)
        still_held = wxGetKeyState(WXK_LEFT) || wxGetKeyState(WXK_NUMPAD_LEFT);
    else if (m_held_arrow_key == WXK_RIGHT)
        still_held = wxGetKeyState(WXK_RIGHT) || wxGetKeyState(WXK_NUMPAD_RIGHT);
    else if (m_held_arrow_key == WXK_UP)
        still_held = wxGetKeyState(WXK_UP) || wxGetKeyState(WXK_NUMPAD_UP);
    else if (m_held_arrow_key == WXK_DOWN)
        still_held = wxGetKeyState(WXK_DOWN) || wxGetKeyState(WXK_NUMPAD_DOWN);

    if (!still_held)
    {
        m_held_arrow_key = 0;
        return;
    }

    // Only move if OS key repeat has stopped (no KEY_DOWN in last 80ms)
    // This prevents double-movement when both timer and KEY_DOWN are firing
    long now = wxGetLocalTimeMillis().GetValue();
    if (now - m_last_arrow_keydown_time < 80)
    {
        // OS repeat is still working, just reschedule and let KEY_DOWN handle it
        m_arrow_repeat_timer.StartOnce(30);
        return;
    }

    // OS repeat has stopped, we take over
    // Calculate delta with current modifiers
    int delta = 1;
    if (wxGetKeyState(WXK_CONTROL))
        delta *= 2;
    if (wxGetKeyState(WXK_SHIFT))
        delta *= 4;

    move_slider_by_key(m_held_arrow_key, delta);

    // Schedule next repeat after a short delay using one-shot timer
    if (m_held_arrow_key != 0)
    {
        m_arrow_repeat_timer.StartOnce(30);
    }
}

void Preview::on_arrow_repeat_timer(wxTimerEvent &event)
{
    do_arrow_repeat();
}

void Preview::move_slider_by_key(int arrow_key, int delta, bool from_gcode_scroll)
{
    if (arrow_key == WXK_LEFT || arrow_key == WXK_RIGHT)
    {
        int current_pos = m_moves_slider->GetHigherPos();
        int min_pos = m_moves_slider->GetMinPos();
        int max_pos = m_moves_slider->GetMaxPos();

        if (arrow_key == WXK_LEFT)
        {
            // Going backward - check if we'll go past the start
            if (current_pos - delta < min_pos)
            {
                // Go to previous layer (down) and set moves to end
                if (m_layers_slider->GetHigherPos() > m_layers_slider->GetMinPos())
                {
                    m_layers_slider->move_current_thumb(1);
                    // Layer 0 is blank build plate - let on_layers_slider_scroll_changed set moves to 0
                    if (m_layers_slider->GetHigherPos() > 0)
                    {
                        m_moves_slider->SetHigherPos(m_moves_slider->GetMaxPos());
                    }
                }
            }
            else
            {
                m_moves_slider->move_current_thumb(delta);
            }
        }
        else
        { // WXK_RIGHT
            // At Layer 0 (blank build plate), immediately jump to Layer 1 start
            // Layer 0 is the empty build plate - skip it for both keyboard and mouse wheel
            if (m_layers_slider->GetHigherPos() == 0)
            {
                m_layers_slider->move_current_thumb(-1);
                m_moves_slider->SetHigherPos(m_moves_slider->GetMinPos());
            }
            // Going forward - check if we'll go past the end
            else if (current_pos + delta > max_pos)
            {
                // Go to next layer (up) and set moves to start
                if (m_layers_slider->GetHigherPos() < m_layers_slider->GetMaxPos())
                {
                    m_layers_slider->move_current_thumb(-1);
                    m_moves_slider->SetHigherPos(m_moves_slider->GetMinPos());
                }
            }
            else
            {
                m_moves_slider->move_current_thumb(-delta);
            }
        }
    }
    else if (arrow_key == WXK_UP || arrow_key == WXK_DOWN)
    {
        m_layers_slider->move_current_thumb(delta * (arrow_key == WXK_DOWN ? 1 : -1));
    }
}

void Preview::update_moves_slider(std::optional<int> visible_range_min, std::optional<int> visible_range_max)
{
    if (active_gcode_result()->moves.empty())
        return;

    const libvgcode::Interval &range = m_canvas->get_gcode_view_enabled_range();
    uint32_t last_gcode_id = m_canvas->get_gcode_vertex_at(range[0]).gcode_id;
    std::optional<uint32_t> gcode_id_min =
        visible_range_min.has_value()
            ? std::optional<uint32_t>{m_canvas->get_gcode_vertex_at(*visible_range_min).gcode_id}
            : std::nullopt;
    std::optional<uint32_t> gcode_id_max =
        visible_range_max.has_value()
            ? std::optional<uint32_t>{m_canvas->get_gcode_vertex_at(*visible_range_max).gcode_id}
            : std::nullopt;

    const size_t range_size = range[1] - range[0] + 1;
    std::vector<unsigned int> values;
    values.reserve(range_size);
    std::vector<unsigned int> alternate_values;
    alternate_values.reserve(range_size);

    std::optional<uint32_t> visible_range_min_id;
    std::optional<uint32_t> visible_range_max_id;
    uint32_t counter = 0;

    for (size_t i = range[0]; i <= range[1]; ++i)
    {
        const uint32_t gcode_id = m_canvas->get_gcode_vertex_at(i).gcode_id;
        bool skip = false;
        if (i > range[0])
        {
            // skip consecutive moves with same gcode id (resulting from processing G2 and G3 lines)
            if (last_gcode_id == gcode_id)
            {
                values.back() = i + 1;
                skip = true;
            }
            else
                last_gcode_id = gcode_id;
        }

        if (!skip)
        {
            values.emplace_back(i + 1);
            alternate_values.emplace_back(gcode_id);
            if (gcode_id_min.has_value() && alternate_values.back() == *gcode_id_min)
                visible_range_min_id = counter;
            else if (gcode_id_max.has_value() && alternate_values.back() == *gcode_id_max)
                visible_range_max_id = counter;
            ++counter;
        }
    }

    const int span_min_id = visible_range_min_id.has_value() ? *visible_range_min_id : 0;
    const int span_max_id = visible_range_max_id.has_value() ? *visible_range_max_id
                                                             : static_cast<int>(values.size()) - 1;

    m_moves_slider->SetSliderValues(values);
    m_moves_slider->SetSliderAlternateValues(alternate_values);

    m_moves_slider->Freeze();
    m_moves_slider->SetMaxPos(static_cast<int>(values.size()) - 1);
    m_moves_slider->SetSelectionSpan(span_min_id, span_max_id);
    m_moves_slider->Thaw();

    m_moves_slider->ShowLowerThumb(get_app_config()->get("seq_top_layer_only") == "0");
}

void Preview::enable_moves_slider(bool enable)
{
    bool render_as_disabled = !enable;
    if (m_moves_slider != nullptr && m_moves_slider->is_rendering_as_disabled() != render_as_disabled)
    {
        m_moves_slider->set_render_as_disabled(render_as_disabled);
    }
}

void Preview::load_print_as_fff(bool keep_z_range)
{
    if (wxGetApp().mainframe == nullptr || wxGetApp().is_recreating_gui())
    {
        // avoid processing while mainframe is being constructed
        return;
    }

    if (m_loaded || m_process->current_printer_technology() != ptFFF)
    {
        return;
    }

    // we require that there's at least one object and the posSlice step
    // is performed on all of them(this ensures that _shifted_copies was
    // populated and we know the number of layers)
    bool has_layers = false;
    const Print *print = m_process->fff_print();
    if (print->is_step_done(posSlice))
    {
        for (const PrintObject *print_object : print->objects())
            if (!print_object->layers().empty())
            {
                has_layers = true;
                break;
            }
    }
    if (print->is_step_done(posSupportMaterial))
    {
        for (const PrintObject *print_object : print->objects())
            if (!print_object->support_layers().empty())
            {
                has_layers = true;
                break;
            }
    }

    if (wxGetApp().is_editor() && !has_layers)
    {
        m_canvas->reset_gcode_toolpaths();
        m_canvas->reset_gcode_layers_times_cache();
        m_canvas->load_gcode_shells();
        hide_layers_slider();
        m_moves_slider->Hide();
        m_canvas_widget->Refresh();
        return;
    }

    libvgcode::EViewType gcode_view_type = m_canvas->get_gcode_view_type();
    const bool gcode_preview_data_valid = !active_gcode_result()->moves.empty();
    const bool is_pregcode_preview = !gcode_preview_data_valid && wxGetApp().is_editor();

    const std::vector<std::string> tool_colors = wxGetApp().plater()->get_extruder_color_strings_from_plater_config(
        active_gcode_result());
    const std::vector<CustomGCode::Item> &color_print_values =
        wxGetApp().is_editor() ? wxGetApp().plater()->model().custom_gcode_per_print_z().gcodes
                               : active_gcode_result()->custom_gcode_per_print_z;

    std::vector<std::string> color_print_colors;
    if (!color_print_values.empty())
    {
        color_print_colors = wxGetApp().plater()->get_color_strings_for_color_print(active_gcode_result());
        color_print_colors.push_back("#808080"); // gray color for pause print or custom G-code
    }

    std::vector<double> zs;

    if (IsShown())
    {
        m_canvas->set_selected_extruder(0);
        if (gcode_preview_data_valid)
        {
            // Load the real G-code preview.
            m_canvas->load_gcode_preview(*active_gcode_result(), tool_colors, color_print_colors);
            // the view type may have been changed by the call m_canvas->load_gcode_preview()
            gcode_view_type = m_canvas->get_gcode_view_type();
            zs = m_canvas->get_gcode_layers_zs();
            m_loaded = true;
        }
        // Only show translucent shells during slicing, then replace with final G-code preview
        // else if (is_pregcode_preview) {
        //     // Load the initial preview based on slices, not the final G-code.
        //     m_canvas->load_preview(tool_colors, color_print_colors, color_print_values);
        //     m_canvas->load_gcode_shells();
        //     // the view type has been changed by the call m_canvas->load_gcode_preview()
        //     if (gcode_view_type == libvgcode::EViewType::ColorPrint && !color_print_values.empty())
        //         m_canvas->set_gcode_view_type(gcode_view_type);
        //     zs = m_canvas->get_gcode_layers_zs();
        // }
        m_moves_slider->Show(gcode_preview_data_valid && !zs.empty());

        if (!zs.empty() && !m_keep_current_preview_type)
        {
            // Always default to FeatureType - user can switch to Tool view if desired
            // Default to Tool view for multi-extruder setups
            const bool contains_color_gcodes = std::any_of(
                std::begin(color_print_values), std::end(color_print_values), [](auto const &item)
                { return item.type == CustomGCode::Type::ColorChange || item.type == CustomGCode::Type::ToolChange; });
            libvgcode::EViewType choice = contains_color_gcodes ? libvgcode::EViewType::ColorPrint
                                                                : libvgcode::EViewType::FeatureType;
            if (choice != gcode_view_type)
            {
                const bool gcode_view_type_cache_load = m_canvas->is_gcode_view_type_cache_load_enabled();
                if (gcode_view_type_cache_load)
                    m_canvas->enable_gcode_view_type_cache_load(false);
                m_canvas->set_gcode_view_type(choice);
                if (gcode_view_type_cache_load)
                    m_canvas->enable_gcode_view_type_cache_load(true);
                if (wxGetApp().is_gcode_viewer())
                    m_keep_current_preview_type = true;
            }
        }

        if (zs.empty())
        {
            // all layers filtered out
            hide_layers_slider();
            m_canvas_widget->Refresh();
        }
        else
            update_layers_slider(zs, keep_z_range);

        // Don't trigger SP_COMPLETED here - this runs at start of reload_print (85%)
        // The actual completion happens in preFlight.GCodeViewer.cpp after rendering finishes (100%)
    }
}

void Preview::load_print_as_sla()
{
    return;
}

void Preview::on_layers_slider_scroll_changed()
{
    if (IsShown())
    {
        PrinterTechnology tech = m_process->current_printer_technology();
        if (tech == ptFFF)
        {
            // Position 0 = Layer 0 (blank build plate, z = 0)
            // Position N = Layer N (shows end of layer N, z = m_values[N-1])
            int lower_pos = m_layers_slider->GetLowerPos();
            int higher_pos = m_layers_slider->GetHigherPos();

            // Get shifted z-values: position 0 → z=0, position N → z=m_values[N-1]
            double z_low = (lower_pos == 0) ? 0.0 : m_layers_slider->GetValueAtPos(lower_pos - 1);
            double z_high = (higher_pos == 0) ? 0.0 : m_layers_slider->GetValueAtPos(higher_pos - 1);

            m_canvas->set_volumes_z_range({z_low, z_high});
            // Toolpaths also need shifted positions: position N shows toolpaths 0 to N-1
            unsigned int tp_low = (lower_pos == 0) ? 0 : static_cast<unsigned int>(lower_pos - 1);
            unsigned int tp_high = (higher_pos == 0) ? 0 : static_cast<unsigned int>(higher_pos - 1);
            m_canvas->set_toolpaths_z_range({tp_low, tp_high});

            // When at layer 0 (minimum position), reset the moves slider to show from the beginning
            // of the file. This allows scrubbing forward through startup G-code moves.
            if (lower_pos == 0 && higher_pos == 0)
            {
                m_moves_slider->SetSelectionSpan(0, 0);
                // Trigger the visual update for the new moves position
                m_canvas->update_gcode_sequential_view_current(
                    static_cast<unsigned int>(m_moves_slider->GetLowerValue() - 1),
                    static_cast<unsigned int>(m_moves_slider->GetHigherValue() - 1));
            }

            m_canvas->set_as_dirty();
        }
        else if (tech == ptSLA)
        {
            m_canvas->set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -m_layers_slider->GetLowerValue()));
            m_canvas->set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), m_layers_slider->GetHigherValue()));
            m_canvas->set_layer_slider_index(m_layers_slider->GetHigherPos());
            m_canvas->render();
        }
    }
}

void Preview::on_moves_slider_scroll_changed()
{
    m_canvas->update_gcode_sequential_view_current(static_cast<unsigned int>(m_moves_slider->GetLowerValue() - 1),
                                                   static_cast<unsigned int>(m_moves_slider->GetHigherValue() - 1));
    m_canvas->set_as_dirty();
    m_canvas->request_extra_frame();
}

} // namespace GUI
} // namespace Slic3r
