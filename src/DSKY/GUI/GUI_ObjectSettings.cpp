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
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "OptionsGroup.hpp"
#include "Plater.hpp"
#include "Sidebar.hpp"
#include "format.hpp"
#include "wxExtensions.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/FlatStaticBox.hpp"
#include "Widgets/ScrollablePanel.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/UIColors.hpp"
#include "luminary/model/scene/Model.hpp"
#include "luminary/presets/bundle/PresetBundle.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"

#include <boost/algorithm/string.hpp>

#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/weakref.h>
#include <wx/wupdlock.h>

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

// The sidebar's sizes: the input width, the lock icon and its margin
static int input_width()
{
    return 7 * wxGetApp().em_unit();
}

static wxSize icon_size()
{
    const int size = int(1.6 * wxGetApp().em_unit());
    return wxSize(size, size);
}

static int icon_margin()
{
    return wxGetApp().em_unit() / 5;
}

// The extruder pseudo-key: the item's extruder, the value the list's column shows. It is not a
// member of the object or region config, so the row is built by hand.
static const char *EXTRUDER_KEY = "extruder";

// ----------------------------------------------------------------------------
// OverrideCategoryBar: the icon strip that switches the panel's pages
// ----------------------------------------------------------------------------

class OverrideCategoryBar : public wxPanel
{
public:
    struct Item
    {
        wxString title;
        std::string icon_name;
        wxBitmapBundle icon;
    };

    explicit OverrideCategoryBar(wxWindow *parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        UpdateAppearance();
        Bind(wxEVT_PAINT, &OverrideCategoryBar::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &OverrideCategoryBar::OnMouseDown, this);
        Bind(wxEVT_MOTION, &OverrideCategoryBar::OnMouseMove, this);
        Bind(wxEVT_LEAVE_WINDOW, &OverrideCategoryBar::OnMouseLeave, this);
    }

    void SetItems(std::vector<Item> items)
    {
        m_items = std::move(items);
        for (Item &item : m_items)
            item.icon = *get_bmp_bundle(item.icon_name);
        m_active = 0;
        m_hovered = -1;
        Refresh();
    }

    void SetActive(int index)
    {
        if (index < 0 || index >= int(m_items.size()) || index == m_active)
            return;
        m_active = index;
        Refresh();
        if (m_on_changed)
            m_on_changed(index);
    }

    int GetActive() const { return m_active; }
    void SetOnChanged(std::function<void(int)> cb) { m_on_changed = std::move(cb); }

    // One count per item, drawn beside the icon while it is above zero
    void SetCounts(std::vector<int> counts)
    {
        m_counts = std::move(counts);
        Refresh();
    }

    void UpdateAppearance()
    {
        for (Item &item : m_items)
            item.icon = *get_bmp_bundle(item.icon_name);
        const int em = wxGetApp().em_unit();
        SetMinSize(wxSize(-1, em * 26 / 10));
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        const int em = wxGetApp().em_unit();
        const wxColour bg = panel_background();
        dc.SetBrush(wxBrush(bg));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
        if (m_items.empty())
            return;

        const wxColour accent = UIColors::AccentPrimary();
        const wxColour raised = bg.ChangeLightness(wxGetApp().dark_mode() ? 115 : 94);
        const int count = int(m_items.size());
        const int width = size.GetWidth() / count;
        const int indicator = std::max(2, em * 3 / 10);
        for (int i = 0; i < count; ++i)
        {
            const int x = i * width;
            const int w = (i == count - 1) ? size.GetWidth() - x : width;
            if (i == m_active || i == m_hovered)
            {
                dc.SetBrush(wxBrush(i == m_active ? raised : raised.ChangeLightness(105)));
                dc.DrawRectangle(x, 0, w, size.GetHeight());
            }
            if (i == m_active)
            {
                dc.SetBrush(wxBrush(accent));
                dc.DrawRectangle(x, size.GetHeight() - indicator, w, indicator);
            }
            // The icon, with the page's override count beside it while it has any
            const wxBitmap icon = m_items[i].icon.GetBitmapFor(this);
            const int count = i < int(m_counts.size()) ? m_counts[i] : 0;
            const wxString count_text = count > 0 ? wxString::Format("%d", count) : wxString();
#ifdef __APPLE__
            const wxSize icon_size = icon.IsOk() ? icon.GetLogicalSize() : wxSize(0, 0);
#else
            const wxSize icon_size = icon.IsOk() ? icon.GetSize() : wxSize(0, 0);
#endif
            dc.SetFont(wxGetApp().small_font().Bold());
            dc.SetTextForeground(accent);
            const wxSize text_size = count_text.IsEmpty() ? wxSize(0, 0) : dc.GetTextExtent(count_text);
            const int gap = count_text.IsEmpty() ? 0 : em / 4;
            const int content_width = icon_size.GetWidth() + gap + text_size.GetWidth();
            const int content_x = x + (w - content_width) / 2;
            const int center_y = (size.GetHeight() - indicator) / 2;
            if (icon.IsOk())
                dc.DrawBitmap(icon, content_x, center_y - icon_size.GetHeight() / 2, true);
            if (!count_text.IsEmpty())
                dc.DrawText(count_text, content_x + icon_size.GetWidth() + gap, center_y - text_size.GetHeight() / 2);
        }
        // The bar's bottom edge
        dc.SetBrush(wxBrush(raised));
        dc.DrawRectangle(0, size.GetHeight() - 1, size.GetWidth(), 1);
    }

    int HitTest(const wxPoint &pt) const
    {
        if (m_items.empty() || pt.x < 0)
            return -1;
        const int width = std::max(1, GetClientSize().GetWidth() / int(m_items.size()));
        const int idx = pt.x / width;
        return idx < int(m_items.size()) ? idx : -1;
    }

    void OnMouseDown(wxMouseEvent &evt)
    {
        const int idx = HitTest(evt.GetPosition());
        if (idx >= 0)
            SetActive(idx);
    }

    void OnMouseMove(wxMouseEvent &evt)
    {
        const int idx = HitTest(evt.GetPosition());
        if (idx != m_hovered)
        {
            m_hovered = idx;
            SetToolTip(idx >= 0 ? m_items[idx].title : wxString());
            Refresh();
        }
    }

    void OnMouseLeave(wxMouseEvent &)
    {
        if (m_hovered != -1)
        {
            m_hovered = -1;
            Refresh();
        }
    }

    std::vector<Item> m_items;
    std::vector<int> m_counts;
    int m_active{0};
    int m_hovered{-1};
    std::function<void(int)> m_on_changed;
};

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

    m_categories = new OverrideCategoryBar(this);
    m_categories->SetOnChanged([this](int index) { show_page(index); });
    main->Add(m_categories, 0, wxEXPAND);

    m_scroll = new ScrollablePanel(this, wxID_ANY);
    m_scroll->sys_color_changed();
    m_scroll->GetContentPanel()->SetSizer(new wxBoxSizer(wxVERTICAL));
    main->Add(m_scroll, 1, wxEXPAND);

    SetSizer(main);
    apply_theme();

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
    if (!m_built || m_built_extruders != extruders_count())
        build_rows();
    refresh();
    if (!m_open)
        return false;
    Show();
    return true;
}

void ObjectSettings::prebuild()
{
    if (m_built && m_built_extruders == extruders_count())
        return;
    wxWindowUpdateLocker no_updates(this);
    build_rows();
    // The object scope's visibility pass and the first layout, while nothing is on screen
    m_scope = OverrideScope::Object;
    apply_scope();
    Layout();
}

void ObjectSettings::close()
{
    m_open = false;
    Hide();
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
    if (!m_built || m_built_extruders != extruders_count())
        build_rows();
    wxWindowUpdateLocker no_updates(this);
    apply_scope();
    refresh_header();
    refresh_rows();
    Layout();
}

// ---- building -------------------------------------------------------------

void ObjectSettings::clear_rows()
{
    m_rows.clear();
    m_pages.clear();
    m_bar_pages.clear();
    m_active_page = -1;
    m_categories->SetItems({});
    wxPanel *content = m_scroll->GetContentPanel();
    content->GetSizer()->Clear(true);
    m_built = false;
}

void ObjectSettings::build_rows()
{
    wxWindowUpdateLocker no_updates(this);
    clear_rows();

    // A click on the panel's dead space commits the field being edited, as in the sidebar. The
    // persistent widgets are bound once, while the content is empty; each page's tree when built.
    Sidebar &sidebar = wxGetApp().sidebar();
    if (!m_dead_space_bound)
    {
        sidebar.BindDeadSpaceHandlers(this);
        m_dead_space_bound = true;
    }

    const size_t extruders = extruders_count();
    wxPanel *content = m_scroll->GetContentPanel();
    content->SetBackgroundColour(panel_background());
    wxSizer *content_sizer = content->GetSizer();

    // Every key an object may carry; a sub-item's scope hides what it cannot (apply_scope)
    for (const SettingPage &page_spec : setting_pages())
    {
        if ((page_spec.presets & SettingPresetPrint) == 0)
            continue;
        std::vector<const SettingRow *> rows;
        for (const SettingRow &row : setting_rows())
            if ((row.presets & SettingPresetPrint) != 0 && row.page != nullptr &&
                std::string_view(row.page) == page_spec.title && row.widget != SettingWidget::Custom &&
                overridable_at(OverrideScope::Object, row.key))
                rows.push_back(&row);
        std::stable_sort(rows.begin(), rows.end(),
                         [](const SettingRow *a, const SettingRow *b) { return a->order < b->order; });
        const bool extruder_row = extruders > 1 && std::string_view(page_spec.title) == "Multiple Extruders";
        if (rows.empty() && !extruder_row)
            continue;

        auto page = std::make_unique<Page>();
        page->title = page_spec.title;
        page->icon = page_spec.icon;
        page->panel = new wxPanel(content, wxID_ANY);
        page->panel->SetBackgroundColour(panel_background());
        page->panel->SetSizer(new wxBoxSizer(wxVERTICAL));

        Group *group = nullptr;
        if (extruder_row)
        {
            group = create_group(*page, "Extruders");
            add_row(*group, page->panel, EXTRUDER_KEY, _L("Extruder"));
        }
        for (const SettingRow *row : rows)
        {
            if (group == nullptr || group->title != row->group)
                group = create_group(*page, row->group);
            const ConfigOptionDef *def = print_config_def.get(row->key);
            add_row(*group, page->panel, row->key, def != nullptr ? _L(def->label) : wxString(row->key));
        }

        content_sizer->Add(page->panel, 0, wxEXPAND);
        page->panel->Hide();
        sidebar.BindDeadSpaceHandlers(page->panel);
        m_pages.push_back(std::move(page));
    }

    m_built = true;
    m_built_extruders = extruders;
    apply_theme();
}

void ObjectSettings::apply_scope()
{
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
                const bool shown = row->key == EXTRUDER_KEY || overridable_at(m_scope, row->key);
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
        std::vector<OverrideCategoryBar::Item> items;
        for (int p : m_bar_pages)
            items.push_back({_L(m_pages[p]->title.c_str()), m_pages[p]->icon, {}});
        m_categories->SetItems(std::move(items));
        m_active_page = m_bar_pages.empty() ? -1 : m_bar_pages.front();
    }
    for (size_t p = 0; p < m_pages.size(); ++p)
        m_pages[p]->panel->Show(int(p) == m_active_page);
    m_scroll->GetContentPanel()->Layout();
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
        if (!weak_header)
            return;
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

    row->accent = new wxPanel(parent, wxID_ANY);
    row->accent->SetMinSize(wxSize(std::max(2, em / 4), -1));
    row->accent->SetBackgroundColour(bg);
    left_sizer->Add(row->accent, 0, wxEXPAND);

    row->enable = new ::CheckBox(parent);
    row->enable->SetBackgroundColour(bg);
    row->enable->Bind(wxEVT_CHECKBOX, [this, row](wxCommandEvent &) { on_enable_changed(*row); });
    left_sizer->Add(row->enable, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, icon_margin());

    row->lock = new wxStaticBitmap(parent, wxID_ANY, *get_bmp_bundle("lock_closed"));
    row->lock->SetMinSize(icon_size());
    row->lock->SetBackgroundColour(bg);
    left_sizer->Add(row->lock, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, icon_margin());

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
        const int max_val = row.def->max < INT_MAX ? int(row.def->max) : 10000;
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

// index is the category bar's; the bar lists the pages the open scope shows
void ObjectSettings::show_page(int index)
{
    if (index < 0 || index >= int(m_bar_pages.size()))
        return;
    wxWindowUpdateLocker no_updates(m_scroll);
    m_active_page = m_bar_pages[index];
    for (size_t i = 0; i < m_pages.size(); ++i)
        m_pages[i]->panel->Show(int(i) == m_active_page);
    m_scroll->GetContentPanel()->Layout();
    m_scroll->ScrollToPosition(0);
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

void ObjectSettings::set_row_state(Row &row, bool checked, bool differs, const wxString &lock_tip)
{
    if (row.enable->GetValue() != checked)
        row.enable->SetValue(checked);
    row.enable->Enable(row.rule_enabled);
    set_tooltip_once(row.enable, row.tip_enable,
                     checked ? _L("Follow the project (remove the override)")
                             : format_wxstr(_L("Override this setting for %1%"), item_name()));
    if (!row.state_known || row.last_checked != checked)
    {
        row.accent->SetBackgroundColour(checked ? UIColors::AccentPrimary() : panel_background());
        row.accent->Refresh();
    }
    if (!row.state_known || row.last_differs != differs)
        row.lock->SetBitmap(*get_bmp_bundle(differs ? "lock_open" : "lock_closed"));
    set_tooltip_once(row.lock, row.tip_lock, lock_tip);
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

    m_scroll->GetContentPanel()->Layout();
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

// The rows are rebuilt for the new theme or scale at once, hidden when the panel is closed, so
// the next open shows them without building
void ObjectSettings::sys_color_changed()
{
    m_close->sys_color_changed();
    wxWindowUpdateLocker no_updates(this);
    clear_rows();
    build_rows();
    if (m_open)
        refresh();
    apply_theme();
}

void ObjectSettings::msw_rescale()
{
    m_close->sys_color_changed();
    m_categories->UpdateAppearance();
    m_scroll->msw_rescale();
    wxWindowUpdateLocker no_updates(this);
    clear_rows();
    build_rows();
    if (m_open)
        refresh();
    Layout();
}

} // namespace DSKY
