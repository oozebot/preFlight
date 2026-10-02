///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral, Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "GUI_ObjectSettings.hpp"
#include "GUI_ObjectList.hpp"

#include "ConfigManipulation.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GuiBudget.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "OptionsGroup.hpp"
#include "Plater.hpp"
#include "Sidebar.hpp"
#include "format.hpp"
#include "wxExtensions.hpp"
#include "Widgets/CategoryBar.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/FlatStaticBox.hpp"
#include "Widgets/RedrawLock.hpp"
#include "Widgets/RowIcons.hpp"
#include "Widgets/ScrollablePanel.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/UIColors.hpp"
#include "luminary/model/scene/Model.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"
#include "luminary/core/Raii.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"

#include <boost/algorithm/string.hpp>

#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/weakref.h>

#include <algorithm>
#include <climits>
#include <string_view>

namespace DSKY
{
using namespace Luminary;

OG_Settings::OG_Settings(wxWindow *parent, const bool staticbox) : m_parent(parent)
{
    wxString title = staticbox ? " " : "";
    m_og = std::make_shared<ConfigOptionsGroup>(parent, title);
}

bool OG_Settings::IsShown()
{
    return m_og->sizer->IsEmpty() ? false : m_og->sizer->IsShown(size_t(0));
}

void OG_Settings::Show(const bool show)
{
    m_og->Show(show);
}

void OG_Settings::Hide()
{
    Show(false);
}

void OG_Settings::UpdateAndShow(const bool show)
{
    Show(show);
}

wxSizer *OG_Settings::get_sizer()
{
    return m_og->sizer;
}

// ----------------------------------------------------------------------------
// Colours and sizes shared by the panel's widgets
// ----------------------------------------------------------------------------

static wxColour panel_background()
{
    return wxGetApp().dark_mode() ? UIColors::PanelBackgroundDark() : UIColors::ContentBackgroundLight();
}

static wxColour panel_foreground()
{
    return wxGetApp().dark_mode() ? UIColors::PanelForegroundDark() : UIColors::PanelForegroundLight();
}

// The sidebar's sizes: the input width, the icon margin and the checked row's accent edge
static int input_width()
{
    return 7 * wxGetApp().em_unit();
}

static int icon_margin()
{
    return wxGetApp().em_unit() / 5;
}

static int accent_width()
{
    return std::max(2, wxGetApp().em_unit() / 4);
}

// A tooltip is set only when it changed: every refresh visits every row, and a tooltip write
// is not free on Windows
static void set_tooltip_once(wxWindow *window, wxString &last, const wxString &tooltip)
{
    if (window == nullptr || tooltip == last)
        return;
    last = tooltip;
    if (tooltip.IsEmpty())
        window->UnsetToolTip();
    else
        window->SetToolTip(tooltip);
}

// The extruder pseudo-key: the item's extruder, the value the list's column shows. It is not a
// member of the object or region config, so the row is built by hand.
static const char *EXTRUDER_KEY = "extruder";

// ----------------------------------------------------------------------------
// ObjectSettings: the override panel
// ----------------------------------------------------------------------------

ObjectSettings::ObjectSettings(wxWindow *parent) : wxPanel(parent, wxID_ANY)
{
    const int em = wxGetApp().em_unit();
    const wxColour bg = panel_background();
    SetBackgroundColour(bg);
    SetForegroundColour(panel_foreground());

    auto *main = new wxBoxSizer(wxVERTICAL);

    // Header: the title names the scope (the item itself is the highlighted row of the list
    // above), Reset all and close on the right
    auto *header = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(this, wxID_ANY, _L("Object Overrides"), wxDefaultPosition, wxDefaultSize,
                               wxST_ELLIPSIZE_END);
    m_title->SetFont(wxGetApp().bold_font());
    m_title->SetBackgroundColour(bg);
    m_title->SetMinSize(wxSize(1, -1));
    // What the panel does not hold, as the title's tooltip rather than a line of its own
    m_title->SetToolTip(_L("Filament, printer and plate settings apply to the whole print."));
    header->Add(m_title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

    m_reset_all = new wxButton(this, wxID_ANY, _L("Reset all"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    wxGetApp().UpdateDarkUI(m_reset_all);
    m_reset_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_reset_all(); });
    header->Add(m_reset_all, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 2);

    m_close = new ScalableButton(this, wxID_ANY, "cross");
    m_close->SetToolTip(_L("Close"));
    m_close->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { wxGetApp().obj_list()->close_overrides(); });
    header->Add(m_close, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, em / 4);
    main->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);

    m_categories = new CategoryBar(this, panel_background);
    m_categories->SetOnChanged([this](int index) { show_page(index); });
    main->Add(m_categories, 0, wxEXPAND);

    m_scroll = new ScrollablePanel(this, wxID_ANY);
    m_scroll->sys_color_changed();
    m_scroll->SetBudgetTag("overrides");
    m_scroll->GetContentPanel()->SetSizer(new wxBoxSizer(wxVERTICAL));
    main->Add(m_scroll, 1, wxEXPAND);

    SetSizer(main);
    apply_theme();

    m_build_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { on_build_timer(); });
    m_layout_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { on_layout_timer(); });

    m_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(this, 1, wxEXPAND);
    Hide();
}

// The sidebar's theming pass over the whole panel, then the few widgets whose colour is not the
// sidebar's text colour: the accent edge, the count chip and the secondary lines and marks.
void ObjectSettings::apply_theme()
{
    ApplySidebarTheme(this);
    for (auto &page : m_pages)
        for (auto &group : page->groups)
        {
            group->title_text->SetForegroundColour(UIColors::AccentPrimary());
            group->badge->SetForegroundColour(UIColors::AccentPrimary());
        }
    m_categories->UpdateAppearance();
    m_scroll->sys_color_changed();
    Refresh();
}

// ---- identity -------------------------------------------------------------

bool ObjectSettings::scope_of(const wxDataViewItem &item, OverrideScope &scope)
{
    if (!item.IsOk())
        return false;
    ObjectDataViewModel *model = wxGetApp().obj_list()->GetModel();
    const ItemType type = model->GetItemType(item);
    if (type & (itObject | itInstance))
    {
        scope = OverrideScope::Object;
        return true;
    }
    if (type & itVolume)
    {
        const ModelVolumeType vt = model->GetVolumeType(item);
        if (vt == ModelVolumeType::MODEL_PART)
            scope = OverrideScope::Part;
        else if (vt == ModelVolumeType::PARAMETER_MODIFIER)
            scope = OverrideScope::Modifier;
        else
            return false;
        return true;
    }
    if (type & itLayer)
    {
        scope = OverrideScope::LayerRange;
        return true;
    }
    return false;
}

int ObjectSettings::override_count(OverrideScope scope, const DynamicPrintConfig &config)
{
    // An explicit extruder counts only on a printer with several: with one there is nothing to
    // choose, and the panel has no Extruder row to show it (imported parts often carry a 1)
    const bool several_extruders = wxGetApp().extruders_edited_cnt() > 1;
    int count = 0;
    for (const std::string &key : config.keys())
    {
        if (key == EXTRUDER_KEY)
        {
            if (several_extruders && config.opt_int(key) != 0)
                ++count;
        }
        else if (overridable_at(scope, key))
            ++count;
    }
    return count;
}

int ObjectSettings::override_count(const wxDataViewItem &item)
{
    OverrideScope scope;
    if (!scope_of(item, scope))
        return 0;
    return override_count(scope, wxGetApp().obj_list()->get_item_config(item).get());
}

ModelObject *ObjectSettings::model_object() const
{
    const ModelObjectPtrs &objects = wxGetApp().model().objects;
    return (m_obj_idx >= 0 && size_t(m_obj_idx) < objects.size()) ? objects[m_obj_idx] : nullptr;
}

ModelConfig *ObjectSettings::item_config() const
{
    ModelObject *object = model_object();
    if (object == nullptr)
        return nullptr;
    switch (m_scope)
    {
    case OverrideScope::Object:
        return &object->config;
    case OverrideScope::Part:
    case OverrideScope::Modifier:
        return (m_vol_idx >= 0 && size_t(m_vol_idx) < object->volumes.size()) ? &object->volumes[m_vol_idx]->config
                                                                              : nullptr;
    case OverrideScope::LayerRange:
    {
        auto it = object->layer_config_ranges.find(m_range);
        return it == object->layer_config_ranges.end() ? nullptr : &it->second;
    }
    }
    return nullptr;
}

const ModelConfig *ObjectSettings::parent_object_config() const
{
    if (m_scope == OverrideScope::Object)
        return nullptr;
    ModelObject *object = model_object();
    return object == nullptr ? nullptr : &object->config;
}

wxDataViewItem ObjectSettings::resolve_item() const
{
    ObjectDataViewModel *model = wxGetApp().obj_list()->GetModel();
    if (model_object() == nullptr)
        return wxDataViewItem(nullptr);
    switch (m_scope)
    {
    case OverrideScope::Object:
        return model->GetItemById(m_obj_idx);
    case OverrideScope::Part:
    case OverrideScope::Modifier:
        return model->GetItemByVolumeId(m_obj_idx, m_vol_idx);
    case OverrideScope::LayerRange:
        return model->GetItemByLayerRange(m_obj_idx, m_range);
    }
    return wxDataViewItem(nullptr);
}

wxString ObjectSettings::item_name() const
{
    ModelObject *object = model_object();
    if (object == nullptr)
        return wxString();
    if ((m_scope == OverrideScope::Part || m_scope == OverrideScope::Modifier) && m_vol_idx >= 0 &&
        size_t(m_vol_idx) < object->volumes.size())
        return from_u8(object->volumes[m_vol_idx]->name);
    return from_u8(object->name);
}

size_t ObjectSettings::extruders_count() const
{
    return size_t(std::max(1, wxGetApp().extruders_edited_cnt()));
}

bool ObjectSettings::is_open_for(const wxDataViewItem &item) const
{
    if (!m_open || !item.IsOk())
        return false;
    OverrideScope scope;
    if (!scope_of(item, scope) || scope != m_scope)
        return false;
    ObjectDataViewModel *model = wxGetApp().obj_list()->GetModel();
    if (model->GetObjectIdByItem(item) != m_obj_idx)
        return false;
    switch (scope)
    {
    case OverrideScope::Object:
        return true;
    case OverrideScope::Part:
    case OverrideScope::Modifier:
        return model->GetVolumeIdByItem(item) == m_vol_idx;
    case OverrideScope::LayerRange:
        return model->GetLayerRangeByItem(item) == m_range;
    }
    return false;
}

// ---- configs --------------------------------------------------------------

const DynamicPrintConfig &ObjectSettings::project_config() const
{
    return wxGetApp().preset_bundle->prints.get_edited_preset().config;
}

DynamicPrintConfig ObjectSettings::inherited_config() const
{
    DynamicPrintConfig config = project_config();
    if (const ModelConfig *parent = parent_object_config(); parent != nullptr)
        for (const std::string &key : parent->keys())
            if (overridable_at(OverrideScope::Object, key))
                config.set_key_value(key, parent->option(key)->clone());
    return config;
}

DynamicPrintConfig ObjectSettings::effective_config() const
{
    DynamicPrintConfig config = inherited_config();
    if (const ModelConfig *own = item_config(); own != nullptr)
        for (const std::string &key : own->keys())
            if (overridable_at(m_scope, key))
                config.set_key_value(key, own->option(key)->clone());
    return config;
}

// ---- open, close, refresh -------------------------------------------------

bool ObjectSettings::open_for(const wxDataViewItem &item)
{
    OverrideScope scope;
    if (!scope_of(item, scope))
        return false;
    ObjectDataViewModel *model = wxGetApp().obj_list()->GetModel();
    const int obj_idx = model->GetObjectIdByItem(item);
    if (obj_idx < 0)
        return false;

    m_scope = scope;
    m_obj_idx = obj_idx;
    m_vol_idx = (scope == OverrideScope::Part || scope == OverrideScope::Modifier) ? model->GetVolumeIdByItem(item)
                                                                                   : -1;
    m_range = scope == OverrideScope::LayerRange ? model->GetLayerRangeByItem(item) : std::make_pair(0.0, 0.0);
    if (item_config() == nullptr)
        return false;

    m_open = true;
    GuiBudget::Span span("overrides.open");
    if (!is_prebuilt())
    {
        // A click that has to build rows: before the start-up prebuild has finished, or after an
        // extruder count change whose rebuild has not; a build under way is finished, not restarted
        GuiBudget::overrides_built_on_open();
        ensure_built();
    }
    refresh();
    if (!m_open)
        return false;
    Show();
    // The pages behind the other categories are laid out at this width while hidden, so the first
    // click on each shows a page that is already laid out. The timer starts after this event: a
    // running Win32 timer is a USER object, and the open itself creates none.
    CallAfter([this]() { m_layout_timer.StartOnce(PREBUILD_TICK_MS); });
    return true;
}

void ObjectSettings::prebuild()
{
    if (is_prebuilt())
    {
        run_prebuilt_callbacks();
        return;
    }
    if (m_open)
    {
        // Rebuilt at once under the open item's scope
        GuiBudget::snapshot("overrides.prebuild.begin");
        refresh();
        GuiBudget::snapshot("overrides.prebuild.end");
        return;
    }
    // A build is already under way
    if (m_build)
        return;
    GuiBudget::snapshot("overrides.prebuild.begin");
    begin_build(true);
    m_build_timer.StartOnce(PREBUILD_TICK_MS);
    if (m_on_prebuild_begin)
        std::exchange(m_on_prebuild_begin, nullptr)();
}

// One chunk of the prebuild per timer event, so input and paint are handled between chunks: the
// rows of a previous build destroyed, then the new rows until the chunk's time is spent, then the
// object scope's visibility pass on its own
void ObjectSettings::on_build_timer()
{
    // The main window is closing: the plater and the sidebar the build reaches are gone
    if (wxGetApp().plater() == nullptr)
        return;
    const auto started = std::chrono::steady_clock::now();
    Luminary::ScopeGuard chunk_end([started]()
                                   { GuiBudget::measure("overrides.prebuild.chunk", GuiBudget::ms_since(started)); });
    // The old rows go before the new ones come, so the two never add up
    if (!destroy_old_pages(std::chrono::milliseconds(PREBUILD_CHUNK_MS)))
    {
        m_build_timer.StartOnce(PREBUILD_TICK_MS);
        return;
    }
    if (m_build)
    {
        if (continue_build(std::chrono::milliseconds(PREBUILD_CHUNK_MS)))
        {
            end_build();
            m_scope_pending = true;
        }
        m_build_timer.StartOnce(PREBUILD_TICK_MS);
        return;
    }
    if (m_scope_pending)
    {
        m_scope_pending = false;
        // The object scope's visibility pass and the first layout, while nothing is on screen, at the
        // width the panel has when open (its column's; the column's height only decides which pages
        // scroll); then the other pages, one per timer event. An open did all of this itself.
        if (!m_open && m_built)
        {
            if (wxWindow *column = GetParent())
                SetSize(column->GetClientSize());
            m_scope = OverrideScope::Object;
            apply_scope();
            Layout();
            // The prebuild ends with the last page laid out (on_layout_timer)
            m_layout_ends_prebuild = true;
            m_layout_timer.StartOnce(PREBUILD_TICK_MS);
            return;
        }
        end_prebuild();
    }
}

void ObjectSettings::end_prebuild()
{
    m_layout_ends_prebuild = false;
    GuiBudget::snapshot("overrides.prebuild.end");
    // The prebuild's completion signal, after this event
    CallAfter([this]() { run_prebuilt_callbacks(); });
}

bool ObjectSettings::is_prebuilt() const
{
    return m_built;
}

void ObjectSettings::on_prebuilt(std::function<void()> done)
{
    if (is_prebuilt())
        done();
    else
        m_prebuilt.emplace_back(std::move(done));
}

void ObjectSettings::run_prebuilt_callbacks()
{
    if (!is_prebuilt())
        return;
    std::vector<std::function<void()>> done;
    done.swap(m_prebuilt);
    for (auto &fn : done)
        fn();
}

void ObjectSettings::on_extruders_changed()
{
    // Nothing is rebuilt: the extruder dropdown is the one row the count decides, refilled in place
    // after the current event, and shown only while there is more than one extruder. A build under
    // way refills it as it ends.
    if (!m_built || m_extruders_update_pending)
        return;
    m_extruders_update_pending = true;
    CallAfter(
        [this]()
        {
            m_extruders_update_pending = false;
            if (!m_built)
                return;
            update_extruder_row();
            apply_scope();
            if (m_open)
                refresh();
        });
}

void ObjectSettings::update_extruder_row()
{
    const int count = int(extruders_count());
    for (auto &row : m_rows)
    {
        // The role extruders' spinners stop at the count
        if (row->def != nullptr && row->def->type == coInt)
            if (auto *spin = dynamic_cast<SpinInput *>(row->control))
            {
                const int min_val = row->def->min > INT_MIN ? int(row->def->min) : 0;
                const int max_val = row->def->max < INT_MAX ? int(row->def->max) : 10000;
                if (const int role_max = wxGetApp().extruder_role_max(row->key, max_val); role_max != max_val)
                    spin->SetRange(min_val, role_max);
            }
        if (row->key == EXTRUDER_KEY)
            if (auto *combo = dynamic_cast<::ComboBox *>(row->control))
            {
                const int selected = combo->GetSelection();
                combo->Clear();
                combo->Append(_L("default"));
                for (int i = 1; i <= count; ++i)
                    combo->Append(wxString::Format("%d", i));
                combo->SetSelection(std::clamp(selected, 0, count));
                // The next refresh writes the row's value against the new entries
                row->state_known = false;
            }
    }
}

void ObjectSettings::close()
{
    m_open = false;
    Hide();
    GuiBudget::snapshot("overrides.close");
}

void ObjectSettings::refresh()
{
    if (!m_open)
        return;
    if (item_config() == nullptr)
    {
        close();
        return;
    }
    if (!is_prebuilt())
        ensure_built();
    RedrawLock no_redraw(this);
    apply_scope();
    refresh_header();
    refresh_rows();
    Layout();
}

// ---- building -------------------------------------------------------------

void ObjectSettings::clear_rows(bool later)
{
    wxPanel *content = m_scroll->GetContentPanel();
    // A build under way retires its half-built page with the others: the page's panel is a child of
    // the content but not yet in its sizer, and its rows are already in m_rows
    if (m_build && m_build->page)
    {
        m_build->page->panel->Hide();
        m_old_pages.push_back(std::move(m_build->page));
        DBG_COUNT_LOAD("OVERRIDES_BUILD_RESTARTED");
        GuiBudget::snapshot("overrides.build.restarted");
    }
    if (later)
    {
        // Detached and hidden now, destroyed a page per chunk (destroy_old_pages); the row records
        // stay until then, since the controls' handlers hold them
        for (auto &page : m_pages)
            page->panel->Hide();
        content->GetSizer()->Clear(false);
        for (auto &page : m_pages)
            m_old_pages.push_back(std::move(page));
        for (auto &row : m_rows)
            m_old_rows.push_back(std::move(row));
    }
    else
    {
        destroy_old_pages(std::nullopt);
        content->GetSizer()->Clear(true);
    }
    m_rows.clear();
    m_pages.clear();
    m_bar_pages.clear();
    m_active_page = -1;
    m_categories->SetItems({});
    m_built = false;
}

bool ObjectSettings::destroy_old_pages(std::optional<std::chrono::milliseconds> budget)
{
    const auto started = std::chrono::steady_clock::now();
    auto spent = [&]()
    {
        return budget && std::chrono::steady_clock::now() - started >= *budget;
    };
    while (!m_old_pages.empty())
    {
        Page &page = *m_old_pages.back();
        // A group at a time: a page is hundreds of windows
        while (!page.groups.empty())
        {
            if (spent())
                return false;
            Group &group = *page.groups.back();
            group.sizer->Clear(true); // the rows' windows
            if (group.header != nullptr)
                group.header->Destroy();
            page.panel->GetSizer()->Detach(group.sizer);
            delete group.sizer; // and its box
            page.groups.pop_back();
        }
        if (spent())
            return false;
        page.panel->Destroy();
        m_old_pages.pop_back();
    }
    m_old_rows.clear();
    return true;
}

void ObjectSettings::build_rows()
{
    count_sync_build();
    RedrawLock no_redraw(this);
    begin_build(false);
    continue_build(std::nullopt);
    end_build();
    // After the current event, so a caller waiting for the rows never runs inside this one
    CallAfter([this]() { run_prebuilt_callbacks(); });
}

void ObjectSettings::ensure_built()
{
    if (is_prebuilt())
        return;
    count_sync_build();
    RedrawLock no_redraw(this);
    destroy_old_pages(std::nullopt);
    if (!m_build)
        begin_build(false);
    continue_build(std::nullopt);
    end_build();
    CallAfter([this]() { run_prebuilt_callbacks(); });
}

// A full build that holds the window until it ends (hundreds of windows): only an open panel
// whose rows must change at once does it, so each one is counted and traced
void ObjectSettings::count_sync_build()
{
    DBG_COUNT_LOAD("OVERRIDES_SYNC_BUILD");
    GuiBudget::snapshot("overrides.build.sync");
}

// The rows are rebuilt for a new theme or scale: closed, in chunks as at start-up, so the next
// open builds nothing; open, at once, since the panel shows them
void ObjectSettings::rebuild()
{
    if (m_open)
    {
        RedrawLock no_redraw(this);
        clear_rows();
        build_rows();
        refresh();
        return;
    }
    GuiBudget::snapshot("overrides.prebuild.begin");
    begin_build(true);
    m_build_timer.StartOnce(PREBUILD_TICK_MS);
}

void ObjectSettings::begin_build(bool chunked)
{
    GuiBudget::snapshot("overrides.build.begin");
    m_build_timer.Stop();
    m_layout_timer.Stop();
    m_scope_pending = false;
    m_layout_ends_prebuild = false;
    const auto clearing = std::chrono::steady_clock::now();
    clear_rows(chunked);
    GuiBudget::measure("overrides.build.cleared", GuiBudget::ms_since(clearing));

    // A click on the panel's dead space commits the field being edited, as in the sidebar. The
    // persistent widgets are bound once, while the content is empty; each page's tree when built.
    if (!m_dead_space_bound)
    {
        wxGetApp().sidebar().BindDeadSpaceHandlers(this);
        m_dead_space_bound = true;
    }
    m_scroll->GetContentPanel()->SetBackgroundColour(panel_background());

    m_build.emplace();
    m_build->extruders = extruders_count();
    // The project's rules, which each new row starts from
    m_build->rule_states = apply_toggle_rules(SettingPresetPrint, project_config(), m_build->extruders);
    for (const ToggleState &state : m_build->rule_states)
        if (state.extruder < 0)
            m_build->rules[state.key] = &state;
}

// Every key an object may carry, page by page and row by row; a sub-item's scope hides what it
// cannot (apply_scope)
bool ObjectSettings::continue_build(std::optional<std::chrono::milliseconds> budget)
{
    if (!m_build)
        return true;
    BuildCursor &build = *m_build;
    const auto started = std::chrono::steady_clock::now();
    const std::vector<SettingPage> &pages = setting_pages();
    wxPanel *content = m_scroll->GetContentPanel();
    while (build.spec_page < pages.size())
    {
        if (budget && std::chrono::steady_clock::now() - started >= *budget)
            return false;
        const SettingPage &page_spec = pages[build.spec_page];
        if (!build.page)
        {
            if ((page_spec.presets & SettingPresetPrint) == 0)
            {
                ++build.spec_page;
                continue;
            }
            build.rows.clear();
            for (const SettingRow &row : setting_rows())
                if ((row.presets & SettingPresetPrint) != 0 && row.page != nullptr &&
                    std::string_view(row.page) == page_spec.title && row.widget != SettingWidget::Custom &&
                    overridable_at(OverrideScope::Object, row.key))
                    build.rows.push_back(&row);
            std::stable_sort(build.rows.begin(), build.rows.end(),
                             [](const SettingRow *a, const SettingRow *b) { return a->order < b->order; });
            // Built at every extruder count; apply_scope shows it while there is more than one
            const bool extruder_row = std::string_view(page_spec.title) == "Multiple Extruders";
            if (build.rows.empty() && !extruder_row)
            {
                ++build.spec_page;
                continue;
            }
            build.page = std::make_unique<Page>();
            build.page->title = page_spec.title;
            build.page->icon = page_spec.icon;
            build.page->panel = new wxPanel(content, wxID_ANY);
            build.page->panel->SetBackgroundColour(panel_background());
            build.page->panel->SetSizer(new wxBoxSizer(wxVERTICAL));
            build.page->panel->Hide();
            build.group = nullptr;
            build.row = 0;
            if (extruder_row)
            {
                build.group = create_group(*build.page, "Extruders");
                prime_row(*add_row(*build.group, build.page->panel, EXTRUDER_KEY, _L("Extruder")));
            }
            continue;
        }
        if (build.row < build.rows.size())
        {
            const SettingRow *row = build.rows[build.row++];
            if (build.group == nullptr || build.group->title != row->group)
                build.group = create_group(*build.page, row->group);
            const ConfigOptionDef *def = print_config_def.get(row->key);
            prime_row(*add_row(*build.group, build.page->panel, row->key,
                               def != nullptr ? _L(def->label) : wxString(row->key)));
            continue;
        }
        // The page is complete
        content->GetSizer()->Add(build.page->panel, 0, wxEXPAND);
        wxGetApp().sidebar().BindDeadSpaceHandlers(build.page->panel);
        GuiBudget::snapshot(std::string("overrides.page.") + build.page->title);
        m_pages.push_back(std::move(build.page));
        ++build.spec_page;
    }
    return true;
}

void ObjectSettings::end_build()
{
    if (!m_build)
        return;
    m_built = true;
    m_build.reset();
    // The extruder count may have changed while the build ran
    update_extruder_row();
    apply_theme();
    GuiBudget::snapshot("overrides.build.end");
}

// A new row shows the project's value with nothing overridden, so the first open for an item
// writes only what differs from that
void ObjectSettings::prime_row(Row &row)
{
    const DynamicPrintConfig &project = project_config();
    if (m_build)
        if (auto it = m_build->rules.find(row.key); it != m_build->rules.end())
        {
            row.rule_enabled = it->second->enabled;
            row.reason = it->second->enabled ? wxString() : _(it->second->reason);
        }
    const std::string value = row.key == EXTRUDER_KEY
                                  ? std::string("0")
                                  : (project.has(row.key) ? project.opt_serialize(row.key) : std::string());
    const bool was_updating = m_updating;
    m_updating = true;
    set_control_value(row, value);
    set_row_state(row, false, false, _L("Same as the project value."));
    m_updating = was_updating;
}

// One hidden page of the bar's categories per timer event, laid out at the width it will be shown
// at, so its first click shows it without laying its rows out
void ObjectSettings::on_layout_timer()
{
    if (!m_built || wxGetApp().plater() == nullptr)
        return;
    for (int p : m_bar_pages)
    {
        if (p == m_active_page)
            continue;
        wxPanel *panel = m_pages[p]->panel;
        const int height = panel->GetSizer()->GetMinSize().y;
        const wxSize size(m_scroll->ContentWidthFor(height), height);
        if (panel->GetSize() == size)
            continue;
        const auto started = std::chrono::steady_clock::now();
        // Hidden, it lays its rows out through its size event and paints nothing
        panel->SetSize(size);
        GuiBudget::measure("overrides.page_layout", GuiBudget::ms_since(started));
        m_layout_timer.StartOnce(PREBUILD_TICK_MS);
        return;
    }
    if (m_layout_ends_prebuild)
        end_prebuild();
}

void ObjectSettings::apply_scope()
{
    const bool several_extruders = extruders_count() > 1;
    std::vector<int> shown_pages;
    for (size_t p = 0; p < m_pages.size(); ++p)
    {
        Page &page = *m_pages[p];
        bool page_shown = false;
        for (auto &group : page.groups)
        {
            bool group_shown = false;
            for (Row *row : group->rows)
            {
                const bool shown = row->key == EXTRUDER_KEY ? several_extruders : overridable_at(m_scope, row->key);
                group->sizer->Show(row->row_sizer, shown, true);
                group_shown = group_shown || shown;
            }
            page.panel->GetSizer()->Show(group->sizer, group_shown, true);
            group->header->Show(group_shown);
            page_shown = page_shown || group_shown;
        }
        if (page_shown)
            shown_pages.push_back(int(p));
    }

    if (shown_pages != m_bar_pages)
    {
        m_bar_pages = shown_pages;
        std::vector<CategoryBar::Item> items;
        for (int p : m_bar_pages)
            items.push_back({_L(m_pages[p]->title.c_str()), m_pages[p]->icon, {}});
        m_categories->SetItems(std::move(items));
        m_active_page = m_bar_pages.empty() ? -1 : m_bar_pages.front();
    }
    for (size_t p = 0; p < m_pages.size(); ++p)
        m_pages[p]->panel->Show(int(p) == m_active_page);
    // Lays the content out once, at the width the scrollbar leaves
    m_scroll->UpdateScrollbar();
}

// The sidebar's group box: a flat box with a blank title and an overlay panel on its top border
// that carries the title, here with the group's override count beside it. The overlay is a child
// of the box's parent (a child of the box would shift the rows out of the border) and follows
// the box on every size or move, the header pointer guarded for the teardown order.
ObjectSettings::Group *ObjectSettings::create_group(Page &page, const char *title)
{
    const int em = wxGetApp().em_unit();
    wxWindow *parent = page.panel;
    auto owned = std::make_unique<Group>();
    Group *group = owned.get();
    group->title = title;

    auto *box = new FlatStaticBox(parent, wxID_ANY, " ");
#ifdef _WIN32
    box->SetBackgroundStyle(wxBG_STYLE_PAINT);
#endif
#ifdef __WXGTK__
    box->SetFont(wxGetApp().normal_font());
#else
    box->SetFont(wxOSX ? wxGetApp().normal_font() : wxGetApp().bold_font());
#endif
    wxGetApp().UpdateDarkUI(box);
    group->sizer = new wxStaticBoxSizer(box, wxVERTICAL);
#if defined(__WXGTK__) || defined(__WXOSX__)
    group->sizer->AddSpacer(em);
#endif

#ifdef __WXOSX__
    const wxColour header_bg = parent->GetBackgroundColour();
#else
    const wxColour header_bg = box->GetBackgroundColour();
#endif
    group->header = new wxPanel(parent, wxID_ANY);
    group->header->SetBackgroundColour(header_bg);
    auto *header_sizer = new wxBoxSizer(wxHORIZONTAL);
    const wxString title_text = _L(title);
    group->title_text = new wxStaticText(group->header, wxID_ANY, title_text);
    group->title_text->SetFont(wxOSX ? wxGetApp().normal_font() : wxGetApp().bold_font());
    group->title_text->SetBackgroundColour(header_bg);
    group->badge = new wxStaticText(group->header, wxID_ANY, wxEmptyString);
    group->badge->SetFont(wxOSX ? wxGetApp().normal_font() : wxGetApp().bold_font());
    group->badge->SetBackgroundColour(header_bg);
    group->badge->Hide();
    header_sizer->Add(group->title_text, 0, wxALIGN_CENTER_VERTICAL);
    header_sizer->Add(group->badge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 2);
    header_sizer->AddSpacer(em / 3);
    group->header->SetSizer(header_sizer);
    group->header->Fit();

    // Centre the header on the top border line, drawn half a text height below the box top
    const int text_height = group->title_text->GetTextExtent(title_text).GetHeight();
    int y_pos = text_height / 2 - group->header->GetSize().GetHeight() / 2 + 1;
#ifdef __WXOSX__
    if (y_pos < 0)
        y_pos = 0;
#endif
    wxWeakRef<wxWindow> weak_header = group->header;
    group->reposition = [weak_header, box, y_pos]()
    {
        // Past the 16-bit window coordinate range, wx moves the header by scrolling the shared parent,
        // which moves every box and re-enters here through their move events; nested calls are skipped.
        static bool s_repositioning = false;
        if (!weak_header || s_repositioning)
            return;
        s_repositioning = true;
        Luminary::ScopeGuard repositioning_guard([]() { s_repositioning = false; });
        const wxPoint p = box->GetPosition();
        weak_header->SetPosition(wxPoint(p.x + 8, p.y + y_pos)); // x+8 matches the static-box label inset
        weak_header->Raise();
    };
    group->reposition();
    box->Bind(wxEVT_SIZE,
              [reposition = group->reposition](wxSizeEvent &evt)
              {
                  reposition();
                  evt.Skip();
              });
    box->Bind(wxEVT_MOVE,
              [reposition = group->reposition](wxMoveEvent &evt)
              {
                  reposition();
                  evt.Skip();
              });

    page.panel->GetSizer()->Add(group->sizer, 0, wxEXPAND | wxALL, em / 4);
    page.groups.push_back(std::move(owned));
    return group;
}

// A row in the sidebar's anatomy: the left half holds the enable checkbox, the lock and the
// label, the right half the control at its left edge
ObjectSettings::Row *ObjectSettings::add_row(Group &group, wxWindow *parent, const std::string &key,
                                             const wxString &label)
{
    const int em = wxGetApp().em_unit();
    const wxColour bg = panel_background();
    auto owned = std::make_unique<Row>();
    Row *row = owned.get();
    row->key = key;
    row->label = label;
    row->def = print_config_def.get(key);
    row->group = &group;

    auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *left_sizer = new wxBoxSizer(wxHORIZONTAL);

    // The accent edge at the row's left and the lock, in one window as tall as the row; the enable
    // checkbox follows the icons as in the sidebar's nullable rows
    row->lock = new RowIcons(parent, false, true, false);
    row->lock->SetBackgroundColour(bg);
    row->lock->SetAccent(bg, accent_width());
    row->lock->SetIcon(RowIcons::Lock, *get_bmp_bundle("lock_closed"));
    left_sizer->Add(row->lock, 0, wxEXPAND);

    row->enable = new ::CheckBox(parent);
    row->enable->SetBackgroundColour(bg);
    row->enable->Bind(wxEVT_CHECKBOX, [this, row](wxCommandEvent &) { on_enable_changed(*row); });
    // Its tooltip names the open item: written as the pointer arrives, not for every row on each open
    row->enable->Bind(wxEVT_ENTER_WINDOW,
                      [this, row](wxMouseEvent &evt)
                      {
                          set_tooltip_once(row->enable, row->tip_enable, enable_tip(row->enable->GetValue()));
                          evt.Skip();
                      });
    left_sizer->Add(row->enable, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, icon_margin());

    row->label_text = new wxStaticText(parent, wxID_ANY, label + ":", wxDefaultPosition, wxDefaultSize,
                                       wxST_ELLIPSIZE_END);
    row->label_text->SetMinSize(wxSize(1, -1));
    row->label_text->SetBackgroundColour(bg);
    left_sizer->Add(row->label_text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);

    row_sizer->Add(left_sizer, 1, wxEXPAND);

    auto *value_sizer = new wxBoxSizer(wxHORIZONTAL);
    row->control = create_control(parent, value_sizer, *row);
    row_sizer->Add(value_sizer, 1, wxEXPAND);

    group.sizer->Add(row_sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, em / 4);
    row->row_sizer = row_sizer;
    group.rows.push_back(row);
    m_rows.push_back(std::move(owned));
    return row;
}

wxWindow *ObjectSettings::create_control(wxWindow *parent, wxSizer *sizer, Row &row)
{
    const int em = wxGetApp().em_unit();
    const wxColour bg = panel_background();

    if (row.key == EXTRUDER_KEY)
    {
        auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16 * em, -1), 0,
                                     nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
        combo->Append(_L("default"));
        for (size_t i = 1; i <= extruders_count(); ++i)
            combo->Append(wxString::Format("%d", int(i)));
        combo->Bind(wxEVT_COMBOBOX, [this, &row](wxCommandEvent &) { on_value_changed(row); });
        sizer->Add(combo, 0, wxALIGN_CENTER_VERTICAL);
        return combo;
    }
    if (row.def == nullptr)
        return nullptr;

    switch (row.def->type)
    {
    case coBool:
    {
        auto *checkbox = new ::CheckBox(parent);
        checkbox->SetBackgroundColour(bg);
        checkbox->Bind(wxEVT_CHECKBOX, [this, &row](wxCommandEvent &) { on_value_changed(row); });
        sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL);
        sizer->AddStretchSpacer(1);
        return checkbox;
    }
    case coEnum:
    {
        auto *combo = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16 * em, -1), 0,
                                     nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
        if (row.def->enum_def && row.def->enum_def->has_labels())
            for (const std::string &enum_label : row.def->enum_def->labels())
                combo->Append(_(enum_label));
        combo->Bind(wxEVT_COMBOBOX, [this, &row](wxCommandEvent &) { on_value_changed(row); });
        sizer->Add(combo, 0, wxALIGN_CENTER_VERTICAL);
        return combo;
    }
    case coInt:
    {
        const int min_val = row.def->min > INT_MIN ? int(row.def->min) : 0;
        // An option naming an extruder stops at the printer's extruder count (update_extruder_row)
        const int max_val = wxGetApp().extruder_role_max(row.key, row.def->max < INT_MAX ? int(row.def->max) : 10000);
        auto *spin = new SpinInput(parent, "0", "", wxDefaultPosition, wxSize(input_width(), -1), 0, min_val, max_val,
                                   0);
        if (row.def->step > 1)
            spin->SetStep(int(row.def->step));
        spin->Bind(wxEVT_SPINCTRL, [this, &row](wxCommandEvent &) { on_value_changed(row); });
        sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL);
        return spin;
    }
    case coFloat:
    case coFloatOrPercent:
    case coPercent:
    {
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, wxSize(input_width(), -1));
        wxGetApp().UpdateDarkUI(text);
        text->Bind(wxEVT_KILL_FOCUS,
                   [this, &row](wxFocusEvent &evt)
                   {
                       evt.Skip();
                       on_value_changed(row);
                   });
        text->Bind(wxEVT_TEXT_ENTER, [this, &row](wxCommandEvent &) { on_value_changed(row); });
        sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL);
        std::string sidetext = row.def->sidetext;
        if (const size_t paren = sidetext.find('('); paren != std::string::npos)
            sidetext = sidetext.substr(0, paren);
        boost::trim(sidetext);
        if (!sidetext.empty())
        {
            auto *unit = new wxStaticText(parent, wxID_ANY, _(sidetext));
            unit->SetBackgroundColour(bg);
            sizer->Add(unit, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 4);
        }
        return text;
    }
    default:
    {
        auto *text = new ::TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, wxDefaultSize);
        text->SetMinSize(wxSize(1, -1));
        wxGetApp().UpdateDarkUI(text);
        text->Bind(wxEVT_KILL_FOCUS,
                   [this, &row](wxFocusEvent &evt)
                   {
                       evt.Skip();
                       on_value_changed(row);
                   });
        text->Bind(wxEVT_TEXT_ENTER, [this, &row](wxCommandEvent &) { on_value_changed(row); });
        sizer->Add(text, 1, wxEXPAND);
        return text;
    }
    }
}

void ObjectSettings::set_control_value(Row &row, const std::string &serialized)
{
    // A control that already shows the value is left alone: a write repaints it
    if (row.control == nullptr || (row.state_known && row.last_value == serialized))
        return;
    row.last_value = serialized;
    if (row.key == EXTRUDER_KEY)
    {
        if (auto *combo = dynamic_cast<::ComboBox *>(row.control))
        {
            int idx = 0;
            try
            {
                idx = std::stoi(serialized);
            }
            catch (...)
            {
            }
            combo->SetSelection(std::clamp(idx, 0, int(combo->GetCount()) - 1));
        }
        return;
    }
    if (row.def == nullptr)
        return;
    switch (row.def->type)
    {
    case coBool:
        if (auto *checkbox = dynamic_cast<::CheckBox *>(row.control))
            checkbox->SetValue(serialized == "1");
        break;
    case coEnum:
        if (auto *combo = dynamic_cast<::ComboBox *>(row.control))
        {
            if (row.def->enum_def && row.def->enum_def->has_values())
            {
                const auto &values = row.def->enum_def->values();
                for (size_t idx = 0; idx < values.size(); ++idx)
                    if (values[idx] == serialized)
                    {
                        combo->SetSelection(int(idx));
                        break;
                    }
            }
        }
        break;
    case coInt:
        if (auto *spin = dynamic_cast<SpinInput *>(row.control))
        {
            try
            {
                spin->SetValue(std::stoi(serialized));
            }
            catch (...)
            {
            }
        }
        break;
    default:
        if (auto *text = dynamic_cast<::TextInput *>(row.control))
            text->SetValue(from_u8(serialized));
        break;
    }
}

std::string ObjectSettings::read_control_value(const Row &row) const
{
    if (row.control == nullptr)
        return std::string();
    if (row.key == EXTRUDER_KEY)
    {
        auto *combo = dynamic_cast<::ComboBox *>(row.control);
        return combo == nullptr ? std::string("0") : std::to_string(std::max(0, combo->GetSelection()));
    }
    if (row.def == nullptr)
        return std::string();
    switch (row.def->type)
    {
    case coBool:
    {
        auto *checkbox = dynamic_cast<::CheckBox *>(row.control);
        return (checkbox != nullptr && checkbox->GetValue()) ? "1" : "0";
    }
    case coEnum:
    {
        auto *combo = dynamic_cast<::ComboBox *>(row.control);
        const int sel = combo == nullptr ? wxNOT_FOUND : combo->GetSelection();
        if (sel != wxNOT_FOUND && row.def->enum_def && row.def->enum_def->has_values() &&
            sel < int(row.def->enum_def->values().size()))
            return row.def->enum_def->values()[sel];
        return std::string();
    }
    case coInt:
    {
        auto *spin = dynamic_cast<SpinInput *>(row.control);
        return spin == nullptr ? std::string() : std::to_string(spin->GetValue());
    }
    default:
    {
        auto *text = dynamic_cast<::TextInput *>(row.control);
        return text == nullptr ? std::string() : into_u8(text->GetValue());
    }
    }
}

void ObjectSettings::select_category(int index)
{
    m_categories->SetActive(index);
}

// index is the category bar's; the bar lists the pages the open scope shows
void ObjectSettings::show_page(int index)
{
    if (index < 0 || index >= int(m_bar_pages.size()))
        return;
    GuiBudget::Span span("overrides.category");
    RedrawLock no_redraw(m_scroll);
    m_active_page = m_bar_pages[index];
    for (size_t i = 0; i < m_pages.size(); ++i)
        m_pages[i]->panel->Show(int(i) == m_active_page);
    m_scroll->ScrollToPosition(0);
    // Lays the content out once, at the width the scrollbar leaves
    m_scroll->UpdateScrollbar();
    Layout();
}

// ---- refreshing -----------------------------------------------------------

void ObjectSettings::refresh_header()
{
    wxString title;
    switch (m_scope)
    {
    case OverrideScope::Object:
        title = _L("Object Overrides");
        break;
    case OverrideScope::Part:
        title = _L("Part Overrides");
        break;
    case OverrideScope::Modifier:
        title = _L("Modifier Overrides");
        break;
    case OverrideScope::LayerRange:
        title = _L("Layer Range Overrides");
        break;
    }
    m_title->SetLabel(title);

    const ModelConfig *own = item_config();
    m_reset_all->Enable(own != nullptr && override_count(m_scope, own->get()) > 0);
}

void ObjectSettings::apply_rules(const DynamicPrintConfig &effective)
{
    for (auto &row : m_rows)
    {
        row->rule_enabled = true;
        row->reason.clear();
    }
    for (const ToggleState &state : apply_toggle_rules(SettingPresetPrint, effective, extruders_count()))
    {
        if (state.extruder >= 0)
            continue;
        for (auto &row : m_rows)
            if (row->key == state.key)
            {
                row->rule_enabled = state.enabled;
                row->reason = state.enabled ? wxString() : _(state.reason);
            }
    }
}

wxString ObjectSettings::enable_tip(bool checked) const
{
    return checked ? _L("Follow the project (remove the override)")
                   : format_wxstr(_L("Override this setting for %1%"), item_name());
}

void ObjectSettings::set_row_state(Row &row, bool checked, bool differs, const wxString &lock_tip)
{
    if (row.enable->GetValue() != checked)
        row.enable->SetValue(checked);
    row.enable->Enable(row.rule_enabled);
    // The checkbox's tooltip names the open item, so it is written when the pointer reaches the
    // checkbox (add_row), and here only for a row whose check changed while it has one
    if (row.state_known && row.last_checked != checked && !row.tip_enable.IsEmpty())
        set_tooltip_once(row.enable, row.tip_enable, enable_tip(checked));
    if (!row.state_known || row.last_checked != checked)
        row.lock->SetAccent(checked ? UIColors::AccentPrimary() : panel_background(), accent_width());
    if (!row.state_known || row.last_differs != differs)
        row.lock->SetIcon(RowIcons::Lock, *get_bmp_bundle(differs ? "lock_open" : "lock_closed"));
    if (lock_tip != row.tip_lock)
    {
        row.tip_lock = lock_tip;
        row.lock->SetTip(RowIcons::Lock, lock_tip);
    }
    if (row.control != nullptr)
        row.control->Enable(checked && row.rule_enabled);

    wxString tooltip = (row.def == nullptr || row.def->tooltip.empty()) ? wxString() : _(row.def->tooltip);
    if (!row.reason.IsEmpty())
        tooltip = tooltip.IsEmpty() ? row.reason : row.reason + "\n\n" + tooltip;
    set_tooltip_once(row.label_text, row.tip_label, tooltip);
    set_tooltip_once(row.control, row.tip_control, tooltip);

    row.state_known = true;
    row.last_checked = checked;
    row.last_differs = differs;
}

void ObjectSettings::refresh_rows()
{
    const ModelConfig *own = item_config();
    if (own == nullptr)
        return;
    // The controls fire while their values are set; the flag mutes the handlers until the end
    struct UpdatingGuard
    {
        bool &flag;
        explicit UpdatingGuard(bool &f) : flag(f) { flag = true; }
        ~UpdatingGuard() { flag = false; }
    } updating(m_updating);

    const DynamicPrintConfig &project = project_config();
    const ModelConfig *parent = parent_object_config();
    const DynamicPrintConfig effective = effective_config();
    apply_rules(effective);

    // The lock reads against the project: closed when the row's value is the project's, open when
    // it differs, by the item's own override or by the parent object's
    for (auto &owned : m_rows)
    {
        Row &row = *owned;
        bool checked = false;
        bool differs = false;
        std::string value;
        wxString lock_tip;
        if (row.key == EXTRUDER_KEY)
        {
            const int own_extruder = own->has(EXTRUDER_KEY) ? own->opt_int(EXTRUDER_KEY) : 0;
            const int parent_extruder = (parent != nullptr && parent->has(EXTRUDER_KEY)) ? parent->opt_int(EXTRUDER_KEY)
                                                                                         : 0;
            checked = own_extruder != 0;
            value = std::to_string(checked ? own_extruder : parent_extruder);
            differs = checked || parent_extruder != 0;
        }
        else
        {
            checked = own->has(row.key);
            value = effective.has(row.key) ? effective.opt_serialize(row.key) : std::string();
            differs = !project.has(row.key) || project.opt_serialize(row.key) != value;
        }
        if (checked)
            lock_tip = differs ? _L("This override differs from the project value.")
                               : _L("This override equals the current project value.");
        else
            lock_tip = differs ? _L("Inherited from the object's own override, not from the project.")
                               : _L("Same as the project value.");
        set_control_value(row, value);
        set_row_state(row, checked, differs, lock_tip);
    }

    for (auto &page : m_pages)
        for (auto &group : page->groups)
        {
            int count = 0;
            for (const Row *row : group->rows)
                if (row->enable->GetValue())
                    ++count;
            group->badge->SetLabel(count > 0 ? wxString::Format("%d", count) : wxString());
            group->badge->Show(count > 0);
            group->header->Fit();
            group->reposition();
        }

    // The category bar shows each page's count beside its icon
    std::vector<int> page_counts;
    for (int p : m_bar_pages)
    {
        int count = 0;
        for (auto &group : m_pages[p]->groups)
            for (const Row *row : group->rows)
                if (row->enable->GetValue())
                    ++count;
        page_counts.push_back(count);
    }
    m_categories->SetCounts(std::move(page_counts));

    m_scroll->UpdateScrollbar();
}

// ---- writes ---------------------------------------------------------------

void ObjectSettings::remove_override(ModelConfig &config, const std::string &key)
{
    if (key == EXTRUDER_KEY)
        write_extruder(config, 0);
    else
        config.erase(key);
}

// The list's extruder column and the panel's extruder row write the same key the same way: a
// layer range always carries the key (0 is the default extruder), an object or a part carries
// it only while it is set.
void ObjectSettings::write_extruder(ModelConfig &config, int extruder)
{
    if (m_scope == OverrideScope::LayerRange)
        config.set_key_value(EXTRUDER_KEY, new ConfigOptionInt(extruder));
    else if (extruder == 0)
        config.erase(EXTRUDER_KEY);
    else
        config.set_key_value(EXTRUDER_KEY, new ConfigOptionInt(extruder));

    if (const wxDataViewItem item = resolve_item(); item.IsOk())
    {
        ObjectDataViewModel *model = wxGetApp().obj_list()->GetModel();
        model->SetExtruder(extruder == 0 ? wxString(_L("default")) : wxString::Format("%d", extruder), item);
        if (m_scope == OverrideScope::Object)
            model->UpdateVolumesExtruderBitmap(item);
    }
}

void ObjectSettings::commit(const wxString &snapshot, const std::string &changed_key,
                            const std::function<void()> &write)
{
    if (item_config() == nullptr)
        return;
    wxGetApp().plater()->take_snapshot(snapshot);
    write();
    if (!changed_key.empty())
        apply_consistency(changed_key);
    // The extruder takes the list column's path (a full plater update); every other key the
    // legacy panel's (reload the scene, schedule the re-slice of the owning object)
    if (changed_key == EXTRUDER_KEY)
        wxGetApp().plater()->update();
    else
        wxGetApp().obj_list()->changed_object(m_obj_idx);
    wxGetApp().obj_list()->update_override_column(resolve_item());
    refresh();
}

// The coupled-key checks the Print surfaces run after an edit, on the item's effective config:
// every key they change that the scope may carry becomes an override marked "auto". The
// Serpentine coupling follows: Serpentine on selects the Athena generator, a generator other
// than Athena turns Serpentine off.
// The coupled-key checks ask the same questions as on the main settings (fill pattern at 100%,
// spiral vase, automatic widths, Serpentine). A key they change becomes an override of the item;
// a declined change leaves the row ticked with the value the check put back, as the main
// settings leave the field.
void ObjectSettings::apply_consistency(const std::string &changed_key)
{
    ModelConfig *own = item_config();
    if (own == nullptr || changed_key == EXTRUDER_KEY)
        return;
    DynamicPrintConfig effective = effective_config();
    DynamicPrintConfig before = effective;
    auto write = [&](const std::string &key, const ConfigOption *option)
    {
        if (overridable_at(m_scope, key))
            own->set_key_value(key, option->clone());
    };
    auto load_config = [&]()
    {
        for (const std::string &key : before.diff(effective))
            write(key, effective.option(key));
        before = effective;
    };
    ConfigManipulation manipulation(load_config, nullptr, nullptr, nullptr, wxGetApp().mainframe);
    manipulation.update_print_fff_config(&effective, true, changed_key);

    // Serpentine: confirm on enable and revert if declined, select the Athena generator on
    // enable, and turn Serpentine off when the generator moves away from Athena
    const bool serpentine = effective.opt_bool("serpentine_enabled");
    const bool athena = effective.opt_enum<PerimeterGeneratorType>("perimeter_generator") ==
                        PerimeterGeneratorType::Athena;
    if (changed_key == "serpentine_enabled" && serpentine)
    {
        MessageDialog dialog(wxGetApp().mainframe,
                             _L("Serpentine prints each region as a single continuous extrusion in place of "
                                "perimeters and infill, overriding most other print settings (perimeters, fill "
                                "density, fill pattern and related options). With a depth limit set, the "
                                "serpentine is confined to a band along the walls and the interior is filled "
                                "with normal infill.\n\n"
                                "Enable Serpentine?"),
                             _L("Serpentine"), wxICON_WARNING | wxYES | wxNO);
        if (dialog.ShowModal() != wxID_YES)
            write("serpentine_enabled", new ConfigOptionBool(false));
        else if (!athena)
            write("perimeter_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Athena));
    }
    else if (changed_key == "perimeter_generator" && serpentine && !athena)
        write("serpentine_enabled", new ConfigOptionBool(false));
}

// A control event may arrive while the rows are being torn down; the row is then gone
bool ObjectSettings::valid_row(const Row *row) const
{
    return std::any_of(m_rows.begin(), m_rows.end(), [row](const std::unique_ptr<Row> &r) { return r.get() == row; });
}

void ObjectSettings::on_enable_changed(Row &row)
{
    if (m_updating || !m_open || !valid_row(&row))
        return;
    ModelConfig *own = item_config();
    if (own == nullptr)
        return;
    const bool checked = row.enable->GetValue();

    if (row.key == EXTRUDER_KEY)
    {
        const bool has = own->has(EXTRUDER_KEY) && own->opt_int(EXTRUDER_KEY) != 0;
        if (checked == has)
            return;
        int seed = 1;
        if (const ModelConfig *parent = parent_object_config();
            parent != nullptr && parent->has(EXTRUDER_KEY) && parent->opt_int(EXTRUDER_KEY) != 0)
            seed = parent->opt_int(EXTRUDER_KEY);
        commit(format_wxstr(checked ? _L("Override: %1%") : _L("Remove override: %1%"), row.label), EXTRUDER_KEY,
               [&]() { write_extruder(*own, checked ? seed : 0); });
        return;
    }

    if (checked == own->has(row.key))
        return;
    if (checked)
    {
        const DynamicPrintConfig inherited = inherited_config();
        if (!inherited.has(row.key))
            return;
        commit(format_wxstr(_L("Override: %1%"), row.label), row.key,
               [&]() { own->set_key_value(row.key, inherited.option(row.key)->clone()); });
    }
    else
        commit(format_wxstr(_L("Remove override: %1%"), row.label), std::string(),
               [&]() { remove_override(*own, row.key); });
}

void ObjectSettings::on_value_changed(Row &row)
{
    if (m_updating || !m_open || !valid_row(&row))
        return;
    ModelConfig *own = item_config();
    if (own == nullptr || !row.enable->GetValue())
        return;
    const std::string value = read_control_value(row);
    // The control now shows what the user typed, not what the last refresh wrote: the next
    // refresh must rewrite it, whether the value is accepted, corrected or put back
    row.last_value.clear();

    if (row.key == EXTRUDER_KEY)
    {
        int extruder = 0;
        try
        {
            extruder = std::stoi(value);
        }
        catch (...)
        {
            return;
        }
        const int current = own->has(EXTRUDER_KEY) ? own->opt_int(EXTRUDER_KEY) : 0;
        if (extruder == current)
            return;
        commit(format_wxstr(_L("Change override: %1%"), row.label), EXTRUDER_KEY,
               [&]() { write_extruder(*own, extruder); });
        return;
    }

    if (!own->has(row.key))
        return;
    std::unique_ptr<ConfigOption> option(own->option(row.key)->clone());
    if (!option->deserialize(value))
    {
        // The text is not a value of this type: show the stored value again
        row.last_value.clear();
        refresh();
        return;
    }
    if (*option == *own->option(row.key))
        return;
    commit(format_wxstr(_L("Change override: %1%"), row.label), row.key,
           [&]() { own->set_key_value(row.key, option.release()); });
}

void ObjectSettings::on_reset_all()
{
    ModelConfig *own = item_config();
    if (own == nullptr)
        return;
    const int count = override_count(m_scope, own->get());
    if (count == 0)
        return;
    const wxString name = item_name();
    MessageDialog dialog(
        this,
        format_wxstr(_L("Remove all %1% overrides from %2%? The item will follow the project settings."), count, name),
        _L("Reset overrides"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    if (dialog.ShowModal() != wxID_YES)
        return;
    commit(format_wxstr(_L("Remove all overrides: %1%"), name), std::string(),
           [&]()
           {
               for (const std::string &key : own->keys())
                   if (key != EXTRUDER_KEY && overridable_at(m_scope, key))
                       own->erase(key);
               if (own->has(EXTRUDER_KEY) && own->opt_int(EXTRUDER_KEY) != 0)
                   write_extruder(*own, 0);
           });
}

// ---- theme and scale ------------------------------------------------------

// A new theme or scale rebuilds the rows (rebuild): every colour and size is the build's, so the
// rows match a fresh start in the new theme without a second path that recolours them
void ObjectSettings::sys_color_changed()
{
    m_close->sys_color_changed();
    rebuild();
    apply_theme();
}

void ObjectSettings::msw_rescale()
{
    m_close->sys_color_changed();
    m_categories->UpdateAppearance();
    m_scroll->msw_rescale();
    rebuild();
    Layout();
}

} // namespace DSKY
