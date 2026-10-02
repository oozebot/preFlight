///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2022 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/ Copyright (c) 2019 Maeyanie @Maeyanie
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include <wx/dataview.h>
#include <wx/panel.h>
#include <wx/timer.h>
#include "wxExtensions.hpp"
#include "luminary/layer/settings_spec/SettingsSpec.hpp"

class wxBoxSizer;
class wxButton;
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
class CategoryBar;
class RowIcons;

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
    // building them; run once after start-up (a theme or scale change rebuilds them the same way).
    void prebuild();
    // The rows exist, so an open builds nothing
    bool is_prebuilt() const;
    // The extruder count may have changed (a printer switch, a project load, an edit of the count):
    // after the current event the extruder dropdown is refilled and shown or hidden, and the open
    // panel refreshed; nothing is rebuilt
    void on_extruders_changed();

    // The scope an item's overrides live at; false for an item that has none.
    static bool scope_of(const wxDataViewItem &item, OverrideScope &scope);
    // The overrides an item carries: the keys of its config the scope allows, plus the extruder
    // when it is not the default. The header chip, the group badges and the list's column agree.
    static int override_count(const wxDataViewItem &item);
    static int override_count(OverrideScope scope, const DynamicPrintConfig &config);

    void sys_color_changed();
    void msw_rescale();

    // For the test hooks only (GuiTestHooks).
    // Runs `done` once the rows exist: now when they do, else at the end of the next prebuild
    void on_prebuilt(std::function<void()> done);
    // Called once as the next chunked prebuild begins (PREFLIGHT_REBUILD_OVERRIDES_AT_MS)
    void set_on_prebuild_begin(std::function<void()> fn) { m_on_prebuild_begin = std::move(fn); }
    // The categories the bar shows for the open item, and a selection made as a click on the bar does
    int category_count() const { return int(m_bar_pages.size()); }
    void select_category(int index);

private:
    struct Group;
    struct Row
    {
        std::string key;
        wxString label;
        const ConfigOptionDef *def{nullptr};
        Group *group{nullptr};
        // The edge that marks a checked row and the lock (closed: the value equals the project's;
        // open: it differs), painted by one window
        RowIcons *lock{nullptr};
        ::CheckBox *enable{nullptr};
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
    // A build under way, resumable row by row: the prebuild runs it in timed chunks, and a click
    // that needs the rows before it ends runs the rest at once
    struct BuildCursor
    {
        size_t extruders{0};
        size_t spec_page{0};                  // index into setting_pages()
        std::vector<const SettingRow *> rows; // the rows of the page being filled
        size_t row{0};                        // the next of them
        std::unique_ptr<Page> page;           // the page being filled
        Group *group{nullptr};                // its last group
        std::vector<ToggleState> rule_states; // the project's rules, the state a new row starts in
        std::unordered_map<std::string, const ToggleState *> rules;
    };

    // The prebuild's time per chunk, and the pause between chunks in which input and paint are
    // handled: a chunk stalls the window for about this long (a row costs a few ms)
    static constexpr int PREBUILD_CHUNK_MS = 40;
    static constexpr int PREBUILD_TICK_MS = 1;

    // Identity of the open item
    OverrideScope m_scope{OverrideScope::Object};
    int m_obj_idx{-1};
    int m_vol_idx{-1};
    std::pair<double, double> m_range{0.0, 0.0};
    bool m_open{false};

    // The rows are built once for every key an object may carry (a sub-item hides the rows its
    // scope cannot) and rebuilt only when the theme or the scale changes
    bool m_built{false};
    bool m_extruders_update_pending{false};         // on_extruders_changed's update is scheduled
    std::vector<std::function<void()>> m_prebuilt;  // waiting for the rows (on_prebuilt)
    std::function<void()> m_on_prebuild_begin;      // set_on_prebuild_begin
    std::optional<BuildCursor> m_build;             // the build under way
    std::vector<std::unique_ptr<Page>> m_old_pages; // a previous build's pages, still to be destroyed
    std::vector<std::unique_ptr<Row>> m_old_rows;   // their rows' records
    wxTimer m_build_timer;                          // drives the prebuild's chunks
    bool m_scope_pending{false};                    // the prebuild's visibility pass is next
    wxTimer m_layout_timer;                         // lays out the hidden pages, one per event
    bool m_layout_ends_prebuild{false};             // the prebuild is done when they are

    bool m_updating{false};         // a refresh in progress: control events are ignored
    bool m_dead_space_bound{false}; // the panel's persistent widgets commit an edit on a click

    wxBoxSizer *m_sizer{nullptr};
    wxStaticText *m_title{nullptr};
    wxButton *m_reset_all{nullptr};
    ScalableButton *m_close{nullptr};
    CategoryBar *m_categories{nullptr};
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
    // Builds every row now, from scratch
    void build_rows();
    // The rows exist when this returns: a build under way is finished, else one is made
    void ensure_built();
    // Counts and traces a synchronous full build (OVERRIDES_SYNC_BUILD, overrides.build.sync)
    void count_sync_build();
    // Rebuilds the rows for a new theme or scale: in chunks while closed, at once while open
    void rebuild();
    // Refills the extruder dropdown for the current extruder count
    void update_extruder_row();
    // Starts a build: the old rows go at once, or with `chunked` a few groups per prebuild chunk
    void begin_build(bool chunked);
    // Destroys the pages of a previous build until the budget is spent (no budget: all); true once
    // none is left
    bool destroy_old_pages(std::optional<std::chrono::milliseconds> budget);
    // Builds until the budget is spent (no budget: to the end); true once the last page is done
    bool continue_build(std::optional<std::chrono::milliseconds> budget);
    void end_build();
    void prime_row(Row &row);
    void on_build_timer();
    void on_layout_timer();
    void end_prebuild();
    void run_prebuilt_callbacks();
    void clear_rows(bool later = false);
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
    // The enable checkbox's tooltip for the open item
    wxString enable_tip(bool checked) const;

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
