///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2022 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/ Copyright (c) 2019 Maeyanie @Maeyanie
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <wx/dataview.h>
#include <wx/panel.h>
#include "wxExtensions.hpp"
#include "luminary/layer/settings_spec/SettingsSpec.hpp"

class wxBoxSizer;
class wxButton;
class wxStaticBitmap;
class wxStaticBoxSizer;
class wxStaticText;
class CheckBox;
class ScrollablePanel;

namespace Luminary
{
class DynamicPrintConfig;
class ModelConfig;
class ModelObject;
class ConfigOptionDef;
} // namespace Luminary
namespace DSKY
{
using namespace Luminary;

class ConfigOptionsGroup;
class OverrideCategoryBar;

class OG_Settings
{
protected:
    std::shared_ptr<ConfigOptionsGroup> m_og;
    wxWindow *m_parent;

public:
    OG_Settings(wxWindow *parent, const bool staticbox);
    virtual ~OG_Settings() {}

    virtual bool IsShown();
    virtual void Show(const bool show);
    virtual void Hide();
    virtual void UpdateAndShow(const bool show);

    virtual wxSizer *get_sizer();
    ConfigOptionsGroup *get_og() { return m_og.get(); }
    const ConfigOptionsGroup *get_og() const { return m_og.get(); }
    wxWindow *parent() const { return m_parent; }
};

// The override panel under the object list: every setting the selected item may carry, as a
// checkbox row rendered from the row specification. Unchecked, the row shows the value the item
// inherits (the project's, or the parent object's override) in a disabled control; checked, the
// control is enabled and the value is the item's own override. The panel serves one item at a
// time, an object, a part, a modifier or a layer range, and stores its identity (indices, not
// pointers) so a rebuilt list, an undo or a redo cannot leave it on a dead item.
class ObjectSettings : public wxPanel
{
public:
    explicit ObjectSettings(wxWindow *parent);
    ~ObjectSettings() override = default;

    wxSizer *get_sizer() const { return m_sizer; }

    // Opens the panel for the item. Returns false when the item cannot carry overrides (a
    // negative volume, a support blocker or enforcer, an info row).
    bool open_for(const wxDataViewItem &item);
    void close();
    bool is_open() const { return m_open; }
    bool is_open_for(const wxDataViewItem &item) const;
    // Reloads every row from the current configs: after a preset switch, an undo or a redo.
    void refresh();
    // Builds the rows while the panel is hidden, so the first open shows them instead of
    // building them; run once after start-up.
    void prebuild();

    // The scope an item's overrides live at; false for an item that has none.
    static bool scope_of(const wxDataViewItem &item, OverrideScope &scope);
    // The overrides an item carries: the keys of its config the scope allows, plus the extruder
    // when it is not the default. The header chip, the group badges and the list's column agree.
    static int override_count(const wxDataViewItem &item);
    static int override_count(OverrideScope scope, const DynamicPrintConfig &config);

    void sys_color_changed();
    void msw_rescale();

private:
    struct Group;
    struct Row
    {
        std::string key;
        wxString label;
        const ConfigOptionDef *def{nullptr};
        Group *group{nullptr};
        wxPanel *accent{nullptr}; // the edge that marks a checked row
        ::CheckBox *enable{nullptr};
        wxStaticBitmap *lock{nullptr}; // closed: the value equals the project's; open: it differs
        wxStaticText *label_text{nullptr};
        wxWindow *control{nullptr};
        wxSizer *row_sizer{nullptr}; // hidden when the open item's scope cannot carry the key
        wxString reason;             // the toggle rule's reason while the rule disables the row
        bool rule_enabled{true};
        // What the row shows now, so a refresh writes only what changed
        bool state_known{false};
        bool last_checked{false};
        bool last_differs{false};
        std::string last_value;
        wxString tip_enable, tip_lock, tip_label, tip_control;
    };
    // The sidebar's group box: a flat box whose title is an overlay panel on the top border, here
    // with the group's override count beside the title
    struct Group
    {
        std::string title;
        wxStaticBoxSizer *sizer{nullptr};
        wxPanel *header{nullptr};
        wxStaticText *title_text{nullptr};
        wxStaticText *badge{nullptr};
        std::function<void()> reposition; // puts the header back on the box border
        std::vector<Row *> rows;
    };
    struct Page
    {
        std::string title;
        std::string icon;
        wxPanel *panel{nullptr};
        std::vector<std::unique_ptr<Group>> groups;
    };

    // Identity of the open item
    OverrideScope m_scope{OverrideScope::Object};
    int m_obj_idx{-1};
    int m_vol_idx{-1};
    std::pair<double, double> m_range{0.0, 0.0};
    bool m_open{false};

    // The rows are built once for every key an object may carry (a sub-item hides the rows its
    // scope cannot) and rebuilt only when the extruder count, the theme or the scale changes
    bool m_built{false};
    size_t m_built_extruders{0};

    bool m_updating{false};         // a refresh in progress: control events are ignored
    bool m_dead_space_bound{false}; // the panel's persistent widgets commit an edit on a click

    wxBoxSizer *m_sizer{nullptr};
    wxStaticText *m_title{nullptr};
    wxButton *m_reset_all{nullptr};
    ScalableButton *m_close{nullptr};
    OverrideCategoryBar *m_categories{nullptr};
    ScrollablePanel *m_scroll{nullptr};
    std::vector<std::unique_ptr<Page>> m_pages;
    std::vector<std::unique_ptr<Row>> m_rows;
    std::vector<int> m_bar_pages; // the pages the category bar shows for the open scope, in bar order
    int m_active_page{-1};        // index into m_pages

    ModelObject *model_object() const;
    ModelConfig *item_config() const;
    const ModelConfig *parent_object_config() const;
    wxDataViewItem resolve_item() const;
    wxString item_name() const;
    size_t extruders_count() const;

    // The project's print config; with the parent object's overrides for a sub-item; with the
    // item's own overrides on top.
    const DynamicPrintConfig &project_config() const;
    DynamicPrintConfig inherited_config() const;
    DynamicPrintConfig effective_config() const;

    void apply_theme();
    void build_rows();
    void clear_rows();
    // Shows the rows the open scope may carry, the groups and pages that keep one, and rebuilds
    // the category bar when the set of pages changed
    void apply_scope();
    Group *create_group(Page &page, const char *title);
    Row *add_row(Group &group, wxWindow *parent, const std::string &key, const wxString &label);
    wxWindow *create_control(wxWindow *parent, wxSizer *sizer, Row &row);
    void set_control_value(Row &row, const std::string &serialized);
    std::string read_control_value(const Row &row) const;
    void show_page(int index);

    void refresh_rows();
    void refresh_header();
    void apply_rules(const DynamicPrintConfig &effective);
    void set_row_state(Row &row, bool checked, bool differs, const wxString &lock_tip);

    // Writes: one undo snapshot, the write into the item's config, the consistency pass, the
    // re-slice of the owning object, then a refresh.
    bool valid_row(const Row *row) const;
    void on_enable_changed(Row &row);
    void on_value_changed(Row &row);
    void on_reset_all();
    void commit(const wxString &snapshot, const std::string &changed_key, const std::function<void()> &write);
    void apply_consistency(const std::string &changed_key);
    void remove_override(ModelConfig &config, const std::string &key);
    void write_extruder(ModelConfig &config, int extruder);
};

} // namespace DSKY

