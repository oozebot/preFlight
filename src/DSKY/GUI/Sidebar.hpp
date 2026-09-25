///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "luminary/presets/preset/Preset.hpp"
#include "GUI.hpp"
#include "Event.hpp"
#include "wxExtensions.hpp" // For ScalableButton, ScalableBitmap

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/scrolwin.h>
#include <wx/splitter.h>
#include <functional>
#include <memory>
#include <map>
#include <string>

class wxStaticText;
class wxComboBox;
class wxButton;
class wxSplitterWindow;
class CheckBox;
class ScrollablePanel;
class SpinInputDouble;
class SwitchButton;

namespace Luminary
{
class DynamicPrintConfig;
class Preset;
class ModelObject;
struct SettingRow;
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

// Action button types for sidebar export/slice buttons
enum class ActionButtonType : int
{
    Reslice,
    Export,
    SendGCode,
    Connect
};

// Forward declarations
class Plater;
class CollapsibleSection;
class PlaterPresetComboBox;
class ObjectList;
class ObjectManipulation;
class ObjectSettings;
class ObjectLayers;
class SlicedInfo;
// ScalableButton is defined in wxExtensions.hpp
class ConfigOptionsGroup;
class FreqChangedParams;
class Tab;
class SidebarTabBar;

/**
 * TabbedSettingsPanel - Base class for settings panels with fixed tab headers
 *
 * Architecture:
 * - Fixed header strip at top with all tab headers (always visible, never scrolls)
 * - Scrollable content area below showing only the active tab's content
 *
 * Subclasses define:
 * - GetTabDefinitions() - returns list of tabs with name, title, icon
 * - BuildTabContent(index) - builds the content panel for each tab
 *
 * The base class handles:
 * - Tab header rendering and click handling
 * - Content area scrolling
 * - Tab switching with proper show/hide
 * - Dark mode styling
 * - DPI scaling
 */
class TabbedSettingsPanel : public wxPanel
{
public:
    TabbedSettingsPanel(wxWindow *parent, Plater *plater);
    virtual ~TabbedSettingsPanel() { *m_alive = false; }

    // Tab switching
    void SwitchToTab(int index);
    void SwitchToTabByName(const wxString &name);
    int GetActiveTabIndex() const { return m_active_tab_index; }
    wxString GetActiveTabName() const;

    // Lifecycle
    virtual void msw_rescale();
    virtual void sys_color_changed();

    // Force content rebuild (e.g., after config change that affects tab visibility)
    void RebuildContent();
    // Rebuild once after the current event, however many requests arrive before it runs; a
    // preset switch asks several times (extruder count, tab visibility, refreshes). `after`
    // runs once the rebuild is done.
    void ScheduleRebuild(std::function<void()> after = {});

    // Update visibility of rows, groups, and sections without rebuilding
    void UpdateSidebarVisibility();

    // Re-assert accent header colors after an external dark-UI pass reset them (build-time fix)
    void ReapplyTitleAccents();

protected:
    // Tab definition - subclasses return a vector of these
    struct TabDefinition
    {
        wxString name;      // Internal name (for persistence/lookup)
        wxString title;     // Display title
        wxString icon_name; // Icon resource name (from get_bmp_bundle)
    };

    // Subclasses MUST implement these
    virtual std::vector<TabDefinition> GetTabDefinitions() = 0;
    virtual wxPanel *BuildTabContent(int tab_index) = 0;

    // Config access - subclasses MUST implement
    virtual DynamicPrintConfig &GetEditedConfig() = 0;
    virtual const DynamicPrintConfig &GetEditedConfig() const = 0;
    virtual const Preset *GetSystemPresetParent() const = 0;
    virtual Tab *GetSyncTab() const = 0;
    virtual Preset::Type GetPresetType() const = 0;

    // Optional: called after tab switch completes
    virtual void OnTabSwitched(int old_index, int new_index) {}

    // Optional: called during sys_color_changed for subclass-specific updates
    virtual void OnSysColorChanged() {}

    // The enable rules of the row specification applied to the edited preset: every row a rule
    // names is enabled or disabled, the rule's reason ahead of the tooltip while it is disabled.
    // Runs after content is built and after every change.
    void ApplyToggleRules();
    // What is not a rule but must precede them (the printer's machine-limits-usage choices)
    virtual void BeforeToggleRules() {}
    virtual size_t ToggleExtrudersCount() const { return 1; }
    virtual void ApplyToggleState(const std::string &registry_key, bool enabled, const wxString &reason) = 0;

    // Optional: called before content is destroyed during RebuildContent()
    // Subclasses should override to clear their m_setting_controls map
    virtual void ClearSettingControls() {}

    // Show/hide rows based on sidebar_visibility config (subclasses override to iterate m_setting_controls)
    virtual void UpdateRowVisibility() {}

    // Non-setting auxiliary rows (buttons, notes) that should hide when all sibling settings are hidden
    // Each pair: (row_sizer to show/hide, parent_sizer that also contains setting rows)
    std::vector<std::pair<wxSizer *, wxSizer *>> m_auxiliary_rows;

    // Per-row pin checkboxes, created in CreateRowUIBase, shown only in edit mode. A vector of
    // (opt_key, checkbox) rather than a map keyed by opt_key: the same opt_key can appear in two
    // groups (e.g. perimeter_generator in both Advanced and Arachne), and a map would drop one
    // checkbox, leaving an orphan that never gets hidden outside edit mode.
    std::vector<std::pair<std::string, wxStaticBitmap *>> m_visibility_checkboxes;

    // Show/hide and refresh the pin checkboxes to match the current edit-mode and pinned state
    void UpdateVisibilityCheckboxes();

    // Access for subclasses
    Plater *GetPlater() const { return m_plater; }
    wxPanel *GetContentArea() const;          // Returns active tab's content panel for child parenting
    wxPanel *GetContentArea(int index) const; // Returns specific tab's content panel for child parenting
    int GetTabCount() const { return static_cast<int>(m_tabs.size()); }
    const wxString &GetTabName(int index) const { return m_tabs[index].definition.name; }

    // Helper for subclasses to trigger content area layout update
    void UpdateContentLayout();

    // Helper for subclasses to apply dark mode styling to content panels
    void ApplyDarkModeToPanel(wxWindow *window);

    // Helper for subclasses to enable/disable a setting control with proper styling
    // Handles Windows-specific wxTextCtrl workaround (SetEditable instead of Enable)
    void ToggleOptionControl(wxWindow *control, bool enable);

    // Applies one rule state to a row of the panel's control map: the control's enabled state,
    // and the reason ahead of the definition's tooltip on the label and the control while the
    // row is disabled (the label stays hoverable when the control does not)
    template<typename Elements>
    void ApplyToggleStateTo(std::map<std::string, Elements> &controls, const std::string &registry_key, bool enabled,
                            const wxString &reason)
    {
        auto it = controls.find(registry_key);
        if (it == controls.end())
            return;
        Elements &ui = it->second;
        ToggleOptionControl(ui.control, enabled);
        ui.disabled_reason = enabled ? wxString() : reason;
        const ConfigOptionDef *def = print_config_def.get(registry_key.substr(0, registry_key.find('#')));
        wxString tooltip = (def == nullptr || def->tooltip.empty()) ? wxString() : from_u8(def->tooltip);
        if (!ui.disabled_reason.IsEmpty())
            tooltip = tooltip.IsEmpty() ? ui.disabled_reason : ui.disabled_reason + "\n\n" + tooltip;
        for (wxWindow *window : {ui.label_text, ui.control})
        {
            if (window == nullptr)
                continue;
            if (tooltip.IsEmpty())
                window->UnsetToolTip();
            else
                window->SetToolTip(tooltip);
        }
    }

    // Helper for subclasses to update undo/lock icons for a setting
    // Uses virtual GetEditedConfig() and GetSystemPresetParent() for config access
    void UpdateUndoUICommon(const std::string &opt_key, wxWindow *undo_icon, wxWindow *lock_icon,
                            const std::string &original_value);

    // Helper struct for row UI creation context
    struct RowUIContext
    {
        wxBoxSizer *row_sizer{nullptr};
        wxBoxSizer *left_sizer{nullptr};
        wxStaticBitmap *lock_icon{nullptr};
        wxStaticBitmap *undo_icon{nullptr};
        wxStaticText *label_text{nullptr};
        wxStaticBitmap *visibility_checkbox{nullptr}; // Pin checkbox, shown only in Edit Visibility mode
        wxString tooltip;
        const ConfigOptionDef *opt_def{nullptr};
    };

    // Creates the common row UI elements (icons, label, sizers)
    // Returns empty context (row_sizer==nullptr) if opt_key not found in config
    RowUIContext CreateRowUIBase(wxWindow *parent, const std::string &opt_key, const wxString &label);

    // Creates the leading pin checkbox (shown only in Edit Visibility mode) and adds it to left_sizer.
    // Used by CreateRowUIBase and by the special row builders so their rows pin/hide like normal rows.
    wxStaticBitmap *AddPinCheckbox(wxWindow *parent, wxSizer *left_sizer, const std::string &opt_key);

    // Build a group box (FlatStaticBox) with an overlay header that hosts the section pin checkbox
    // (mirrors the main-settings section checkbox). The checkbox toggles every row in the group.
    wxStaticBoxSizer *CreateFlatStaticBoxSizer(wxWindow *parent, const wxString &label, int orient = wxVERTICAL);

    // The spec renderer, the one builder behind every page of every panel: the rows of this
    // panel's preset whose sidebar page is `spec_page`, in sidebar order; a group box per run of
    // rows sharing a group title (the sidebar's own title where the spec carries one, the Settings
    // page's otherwise); every row through CreateSpecRow with the sidebar's own label where the
    // spec carries one and the definition's label otherwise. The hooks below let a panel host
    // groups in sub-panels and add the widgets that are not rows; their defaults do nothing.
    wxPanel *BuildPageFromSpec(const std::string &spec_page, size_t extruder_idx = 0);
    struct SpecGroupHost
    {
        wxWindow *parent{nullptr};
        wxSizer *sizer{nullptr};
    };
    // The row itself, through the panel's row factories.
    virtual void CreateSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, const wxString &label,
                               size_t extruder_idx) = 0;
    // A row the panel leaves out in the current state (extruder offsets with one extruder).
    virtual bool SpecRowShown(const SettingRow &row) const { return true; }
    // Where a group whose first row is `first_row` is built: the page content by default, a
    // sub-panel for the machine limits of one firmware family.
    virtual SpecGroupHost SpecGroupHostFor(wxWindow *content, wxSizer *sizer, const SettingRow &first_row)
    {
        return {content, sizer};
    }
    // Widgets placed before a group on its host, inside a group before its rows, and after a row.
    virtual void BeforeSpecGroup(wxWindow *parent, wxSizer *sizer, const SettingRow &first_row, size_t extruder_idx) {}
    virtual void OnSpecGroupOpened(wxWindow *parent, wxSizer *group, const SettingRow &first_row) {}
    virtual void AfterSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, size_t extruder_idx) {}

    // Section-header pin checkbox: ticking/unticking toggles all rows in its group. Shown only in edit mode.
    struct SectionCheckbox
    {
        ::CheckBox *checkbox{nullptr};
        wxSizer *group_sizer{nullptr};
        wxWindow *header_panel{nullptr};
        wxWindow *box{nullptr};       // the FlatStaticBox the header overlays (for positioning)
        wxStaticText *label{nullptr}; // overlay group-box title (accent-colored; re-applied on theme change)
        int y_pos{0};
        int full_w{0};  // header width with the checkbox shown (edit mode)
        int label_w{0}; // header width with only the label (normal mode)
        int height{0};
    };
    std::vector<SectionCheckbox> m_section_checkboxes;

    void UpdateSectionCheckboxes();                         // show/hide + refresh tri-state from the group's rows
    void SetGroupPinned(wxSizer *group_sizer, bool pinned); // pin/unpin every row in the group

    // Binds undo icon click handler to revert the setting value
    void BindUndoHandler(wxStaticBitmap *undo_icon, const std::string &opt_key,
                         std::function<void(const std::string &)> on_setting_changed);

    // Called by derived class constructors after vtable is set up
    void BuildUI();

private:
    void EnsureContentBuilt(int index);
    void UpdateSizerProportions();

    // Category fold state. Every category is open by default; a header click stores its state
    // under [sidebar_expanded] <panel>/<category>, so the sidebar reopens the way it was left.
    std::string CategoryStateKey(const TabDefinition &def) const;
    bool IsCategoryStoredExpanded(const TabDefinition &def) const;
    void StoreCategoryExpanded(const TabDefinition &def, bool expanded);
    void SetCategoryExpanded(size_t index, bool expanded);     // programmatic: stores nothing
    void OnCategoryExpandChanged(size_t index, bool expanded); // header click
    void ApplyCategoryStates();                                // edit mode opens all, else stored states
    bool m_applying_category_states{false};                    // mutes the click callback during the above

    Plater *m_plater;
    wxBoxSizer *m_main_sizer{nullptr};
    ScrollablePanel *m_scroll_area{nullptr}; // Single scroll area for all sections

    // Tab state - each tab is a non-collapsible section header with content
    struct TabState
    {
        TabDefinition definition;
        CollapsibleSection *section{nullptr};
        wxPanel *content_container{nullptr}; // Plain panel inside section for content parenting
        wxPanel *content{nullptr};
        bool content_built{false};
    };
    std::vector<TabState> m_tabs;
    int m_active_tab_index{0}; // Only used temporarily during EnsureContentBuilt()

public:
    // Registry dump (PREFLIGHT_DUMP_SIDEBAR): one line per row in creation order with the page,
    // group and order the row was built into, the widget class, its shown and enabled state and
    // the edited value. The before / after files of a layout refactor must match line for line.
    struct RowPlacement
    {
        std::string key; // the registry key (a per-extruder row carries "#<extruder>")
        std::string page;
        std::string group;
        int order{0};
    };
    struct RegistryRowInfo
    {
        std::string widget_class;
        bool shown{false};
        bool enabled{false};
        std::string reason; // the rule's reason while disabled
    };
    void EnsureAllContentBuilt();
    void DumpRegistry(const std::string &path) const;

protected:
    // Called by every subclass right where it inserts into its m_setting_controls.
    void NoteRowPlacement(const std::string &registry_key);
    virtual bool LookupRegistryRow(const std::string &registry_key, RegistryRowInfo &out) const = 0;
    std::vector<RowPlacement> m_row_placements;
    std::string m_building_group; // the title of the group being built (CreateFlatStaticBoxSizer)

private:
    // ScheduleRebuild(): one queued rebuild at a time, and the queued callback checks that the
    // panel still exists.
    bool m_rebuild_pending{false};
    std::vector<std::function<void()>> m_after_rebuild;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/**
 * PrintSettingsPanel - Print settings with fixed tab headers
 *
 * Tabs: Layers, Infill, Skirt/Brim, Support, Speed, Extruders, Advanced, Output
 */
class PrintSettingsPanel : public TabbedSettingsPanel
{
public:
    PrintSettingsPanel(wxWindow *parent, Plater *plater);

    void RefreshFromConfig();
    void ResetOriginalValues();

    // Override base class
    void msw_rescale() override;
    void sys_color_changed() override;

protected:
    // TabbedSettingsPanel interface
    std::vector<TabDefinition> GetTabDefinitions() override;
    wxPanel *BuildTabContent(int tab_index) override;
    void OnSysColorChanged() override;
    void ClearSettingControls() override { m_setting_controls.clear(); }
    bool LookupRegistryRow(const std::string &registry_key, RegistryRowInfo &out) const override;

    // Config access (implements TabbedSettingsPanel abstract methods)
    DynamicPrintConfig &GetEditedConfig() override;
    const DynamicPrintConfig &GetEditedConfig() const override;
    const Preset *GetSystemPresetParent() const override;
    Tab *GetSyncTab() const override;
    Preset::Type GetPresetType() const override { return Preset::TYPE_PRINT; }

private:
    // The spec renderer's hooks: the row factory, and the "Set all widths" button after its row
    void CreateSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, const wxString &label,
                       size_t extruder_idx) override;
    void AfterSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, size_t extruder_idx) override;

    // Setting row helpers
    void CreateSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                          bool full_width = false);
    void CreateMultilineSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                                   int num_lines = 5);
    void OnSettingChanged(const std::string &opt_key);
    void UpdateUndoUI(const std::string &opt_key);
    void ApplyToggleState(const std::string &registry_key, bool enabled, const wxString &reason) override;

    // Per-setting UI elements
    struct SettingUIElements
    {
        wxWindow *control{nullptr};
        wxWindow *lock_icon{nullptr};
        wxWindow *undo_icon{nullptr};
        wxWindow *label_text{nullptr};
        std::string original_value;
        wxString disabled_reason;       // the rule's reason while a rule disables the row
        wxSizer *row_sizer{nullptr};    // The row's top-level sizer (for show/hide)
        wxSizer *parent_sizer{nullptr}; // The group sizer containing this row
    };
    std::map<std::string, SettingUIElements> m_setting_controls;

    // Flag to prevent cascading events during RefreshFromConfig
    bool m_disable_update{false};

    void UpdateRowVisibility() override;
};

/**
 * PrinterSettingsPanel - Printer settings with fixed tab headers
 *
 * Tabs: General, Machine Limits, Extruder 1 [, Extruder 2, ...]
 * Note: Extruder tabs are dynamic based on printer's extruder count
 */
class PrinterSettingsPanel : public TabbedSettingsPanel
{
public:
    PrinterSettingsPanel(wxWindow *parent, Plater *plater);
    ~PrinterSettingsPanel();

    void RefreshFromConfig();
    void ResetOriginalValues();

    // Override base class
    void msw_rescale() override;
    void sys_color_changed() override;

    // Called when extruder count changes - rebuilds tabs
    void UpdateExtruderCount(size_t count);

protected:
    // TabbedSettingsPanel interface
    std::vector<TabDefinition> GetTabDefinitions() override;
    wxPanel *BuildTabContent(int tab_index) override;
    void OnSysColorChanged() override;
    bool LookupRegistryRow(const std::string &registry_key, RegistryRowInfo &out) const override;
    void ClearSettingControls() override
    {
        m_setting_controls.clear();
        m_marlin_limits_panel = nullptr;
        m_rrf_limits_panel = nullptr;
        m_klipper_limits_panel = nullptr;
        m_stealth_mode_note = nullptr;
    }

    // Config access (implements TabbedSettingsPanel abstract methods)
    DynamicPrintConfig &GetEditedConfig() override;
    const DynamicPrintConfig &GetEditedConfig() const override;
    const Preset *GetSystemPresetParent() const override;
    Tab *GetSyncTab() const override;
    Preset::Type GetPresetType() const override { return Preset::TYPE_PRINTER; }

private:
    // The spec renderer's hooks: the row factories (per-extruder rows on the extruder pages), the
    // extruder offset only with several extruders, the machine limits of each firmware family in
    // its own sub-panel, and the widgets that are not rows: the bed shape row, the extruder count
    // spinner, the "apply to other extruders" button, the notes and the RRF retrieve button
    void CreateSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, const wxString &label,
                       size_t extruder_idx) override;
    bool SpecRowShown(const SettingRow &row) const override;
    SpecGroupHost SpecGroupHostFor(wxWindow *content, wxSizer *sizer, const SettingRow &first_row) override;
    void BeforeSpecGroup(wxWindow *parent, wxSizer *sizer, const SettingRow &first_row, size_t extruder_idx) override;
    void OnSpecGroupOpened(wxWindow *parent, wxSizer *group, const SettingRow &first_row) override;
    void AfterSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, size_t extruder_idx) override;
    void AddBedShapeRow(wxWindow *parent, wxSizer *group);
    void AddExtruderCountRow(wxWindow *parent, wxSizer *group);
    void AddApplyToOtherExtrudersButton(wxWindow *parent, wxSizer *sizer, size_t extruder_idx);

    // Helper to check if Single Extruder MM tab should be shown
    bool ShouldShowSingleExtruderMM() const;

    void UpdateMachineLimitsVisibility();
    void OnRetrieveFromMachine();

    // Setting row helpers
    void CreateSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                          bool full_width = false);
    void CreateMultilineSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                                   int num_lines = 5);
    void OnSettingChanged(const std::string &opt_key);
    void UpdateUndoUI(const std::string &opt_key);
    void ApplyToggleState(const std::string &registry_key, bool enabled, const wxString &reason) override;
    size_t ToggleExtrudersCount() const override { return m_extruders_count; }
    // The machine-limits-usage choices follow the firmware flavour; refreshed before the rules run
    void BeforeToggleRules() override { UpdateMachineLimitsUsageChoices(); }
    void UpdateMachineLimitsUsageChoices();

    void CreateExtruderSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                                  size_t extruder_idx);
    void OnExtruderSettingChanged(const std::string &opt_key, size_t extruder_idx);

    // Machine limits sub-panels (show/hide based on gcode_flavor)
    wxPanel *m_marlin_limits_panel{nullptr};
    wxPanel *m_rrf_limits_panel{nullptr};
    wxPanel *m_klipper_limits_panel{nullptr};
    wxStaticText *m_stealth_mode_note{nullptr};

    // Extruder tracking
    size_t m_extruders_count{1};

    // Per-setting UI elements
    struct SettingUIElements
    {
        wxWindow *control{nullptr};
        wxWindow *lock_icon{nullptr};
        wxWindow *undo_icon{nullptr};
        wxWindow *label_text{nullptr};
        std::string original_value;
        wxString disabled_reason;       // the rule's reason while a rule disables the row
        wxSizer *row_sizer{nullptr};    // The row's top-level sizer (for show/hide)
        wxSizer *parent_sizer{nullptr}; // The group sizer containing this row
    };
    std::map<std::string, SettingUIElements> m_setting_controls;

    void UpdateRowVisibility() override;

    // Preserved original values that persist across rebuilds
    // Used to maintain undo state when content is rebuilt (e.g., after extruder count change)
    std::map<std::string, std::string> m_preserved_original_values;

    // Flag to prevent cascading events during RefreshFromConfig
    bool m_disable_update{false};

    // Safety flag for CallAfter - prevents use-after-free if panel is destroyed while callback is pending
    std::shared_ptr<bool> m_prevent_call_after_crash = std::make_shared<bool>(true);
};

/**
 * FilamentSettingsPanel - Filament settings with fixed tab headers
 *
 * Tabs: Filament, Cooling, Advanced, Overrides
 */
class FilamentSettingsPanel : public TabbedSettingsPanel
{
public:
    FilamentSettingsPanel(wxWindow *parent, Plater *plater);

    void RefreshFromConfig();
    void ResetOriginalValues();

    // Override base class
    void msw_rescale() override;
    void sys_color_changed() override;

protected:
    // TabbedSettingsPanel interface
    std::vector<TabDefinition> GetTabDefinitions() override;
    wxPanel *BuildTabContent(int tab_index) override;
    void OnSysColorChanged() override;
    bool LookupRegistryRow(const std::string &registry_key, RegistryRowInfo &out) const override;
    void ClearSettingControls() override
    {
        m_setting_controls.clear();
        m_override_checkboxes.clear();
    }

    // Config access (implements TabbedSettingsPanel abstract methods)
    DynamicPrintConfig &GetEditedConfig() override;
    const DynamicPrintConfig &GetEditedConfig() const override;
    const Preset *GetSystemPresetParent() const override;
    Tab *GetSyncTab() const override;
    Preset::Type GetPresetType() const override { return Preset::TYPE_FILAMENT; }

private:
    // The spec renderer's hooks: the row factories (the enable-checkbox row for nullable keys),
    // and the ramming row after the last single-extruder MMU row
    void CreateSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, const wxString &label,
                       size_t extruder_idx) override;
    void AfterSpecRow(wxWindow *parent, wxSizer *group, const SettingRow &row, size_t extruder_idx) override;

    // Setting row helpers
    void CreateSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                          bool full_width = false);
    void CreateMultilineSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                                   int num_lines = 5);
    void CreateNullableSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label);
    void OnSettingChanged(const std::string &opt_key);
    void OnNullableSettingChanged(const std::string &opt_key, bool is_checked);
    void UpdateUndoUI(const std::string &opt_key);
    void UpdateOverridesToggleState();
    void ApplyToggleState(const std::string &registry_key, bool enabled, const wxString &reason) override;

    // Per-setting UI elements
    struct SettingUIElements
    {
        wxWindow *control{nullptr};
        wxWindow *lock_icon{nullptr};
        wxWindow *undo_icon{nullptr};
        wxWindow *label_text{nullptr};
        ::CheckBox *enable_checkbox{nullptr}; // For nullable options
        std::string original_value;
        wxString disabled_reason;          // the rule's reason while a rule disables the row
        std::string last_meaningful_value; // Last non-nil value
        wxSizer *row_sizer{nullptr};       // The row's top-level sizer (for show/hide)
        wxSizer *parent_sizer{nullptr};    // The group sizer containing this row
    };
    std::map<std::string, SettingUIElements> m_setting_controls;

    // Map of nullable option checkboxes for quick access
    std::map<std::string, ::CheckBox *> m_override_checkboxes;

    // Flag to prevent cascading events during RefreshFromConfig
    bool m_disable_update{false};

    void UpdateRowVisibility() override;
};

/**
 * ProcessSection - The Print Settings section wrapper
 *
 * Contains:
 * - Print preset selector
 * - PrintSettingsPanel with nested category accordions
 */
class ProcessSection : public wxPanel
{
public:
    ProcessSection(wxWindow *parent, Plater *plater);
    PrintSettingsPanel *settings_panel() const { return m_settings_panel; }

    void SetPresetComboBox(PlaterPresetComboBox *combo);
    void UpdateFromConfig();
    void ResetOriginalValues();
    void RebuildContent();
    void UpdateSidebarVisibility();

    void msw_rescale();
    void sys_color_changed();
    void ReapplyTitleAccents();

private:
    void BuildUI();
    void OnSavePreset();

    Plater *m_plater;
    PlaterPresetComboBox *m_preset_combo;
    PrintSettingsPanel *m_settings_panel;
    ScalableButton *m_btn_save;

    wxBoxSizer *m_main_sizer;
};

/**
 * Sidebar - Modern resizable sidebar with collapsible sections
 *
 * Architecture:
 * - Resizable via splitter or drag handle
 * - Collapsible accordion sections: Printer, Filament, Process, Objects
 * - Inline settings editing in Process section
 * - Clean separation of concerns
 */
class Sidebar : public wxPanel
{
public:
    Sidebar(Plater *parent);
    ~Sidebar();

    // Accessors for compatibility with existing code
    Plater *plater() const { return m_plater; }
    ObjectList *obj_list() const { return m_object_list; }
    // Registry dump (PREFLIGHT_DUMP_SIDEBAR): the three panels in both layouts and both
    // visibility modes, twelve files under dir; the layout and mode in force are restored after.
    void dump_settings_registry(const std::string &dir);
    void SetEditVisibilityMode(bool edit);
    ObjectManipulation *obj_manipul() const { return m_object_manipulation; }
    ObjectSettings *obj_settings() const { return m_object_settings; }
    ObjectLayers *obj_layers() const { return m_object_layers; }

    // Preset management
    void update_presets(Preset::Type preset_type);
    void update_all_preset_comboboxes();
    void update_printer_presets_combobox();
    void update_all_filament_comboboxes();

    // Extruder/filament management
    void set_extruders_count(size_t count);
    void update_objects_list_extruder_column(size_t count);

    // UI state
    void collapse(bool collapse);

    // Public member for direct access compatibility with old Sidebar
    bool is_collapsed{false};

    void show_sliced_info_sizer(bool show);
    void show_btns_sizer(bool show);

    // When object settings are shown, minimize ObjectList to save space
    void set_object_settings_mode(bool settings_visible);
    void update_sliced_info_sizer();

    // Compatibility methods (may return nullptr if not applicable)
    ConfigOptionsGroup *og_freq_chng_params();
    wxButton *get_wiping_dialog_button();

    // Buttons - single export
    void enable_buttons(bool enable);
    bool show_reslice(bool show);
    bool show_export(bool show);
    bool show_send(bool show);
    bool show_export_removable(bool show);
    bool show_connect(bool show);
    void set_btn_label(ActionButtonType type, const wxString &label);

    // Buttons - bulk export
    bool show_export_all(bool show);
    bool show_connect_all(bool show);
    bool show_export_removable_all(bool show);
    void enable_bulk_buttons(bool enable);

    // Autoslicing mode
    void switch_to_autoslicing_mode();
    void switch_from_autoslicing_mode();

    // Mode (Simple/Advanced/Expert)
    void update_mode();
    void update_ui_from_settings();

    // Tabbed/unified sidebar mode
    void SetTabbedMode(bool tabbed);

    // Scaling
    void msw_rescale();
    void sys_color_changed();

    // Fixed default expanded state for each section
    void LoadSectionStates();

    // Rebuild settings panels (call when sidebar visibility settings change)
    void rebuild_settings_panels();

    // Update sidebar visibility without rebuilding (show/hide rows, groups, sections in-place)
    void update_sidebar_visibility();

    // Refresh the sidebar settings panel for the given preset type (Tab -> Sidebar sync)
    void refresh_settings_panel(Preset::Type type, bool reset_original_values = false);

    // Section access
    CollapsibleSection *GetPrinterSection() { return m_printer_section; }
    CollapsibleSection *GetFilamentSection() { return m_filament_section; }
    CollapsibleSection *GetProcessSection() { return m_process_section; }
    CollapsibleSection *GetObjectsSection() { return m_objects_section; }

    // Bind click handlers on container panels to commit field changes when clicking dead space
    // Call this on newly created content panels to enable the behavior
    void BindDeadSpaceHandlers(wxWindow *root);

    // Refresh printer nozzle spinners and accordion panel
    void refresh_printer_nozzles()
    {
        UpdatePrinterFilamentCombos();
        if (m_printer_settings_panel)
            m_printer_settings_panel->RefreshFromConfig();
    }

private:
    void BuildUI();
    void CreatePrinterSection();
    void CreateFilamentSection();
    void CreateProcessSection();
    void CreateObjectsSection();

    void CreateInfoSections();

    void OnSectionExpandChanged(const wxString &section_name, bool expanded);
    void ApplyTabVisibility();

    // Preset combo selection handler
    void on_select_preset(wxCommandEvent &evt);

    // Filament combo management
    void init_filament_combo(PlaterPresetComboBox **combo, int extr_idx);
    void remove_unused_filament_combos(size_t current_count);

    // Printer section filament combos (for quick extruder filament selection)
    void UpdatePrinterFilamentCombos();
    void init_printer_filament_combo(PlaterPresetComboBox **combo, int extr_idx);
    void update_nozzle_undo_ui(size_t idx);
    void update_all_nozzle_undo_ui();

    // Bottom button bar
    void CreateButtonBar();
    void OnViewModeToggle(wxCommandEvent &evt);
    void OnEditModeToggle(wxCommandEvent &evt);

    Plater *m_plater;

    // Main layout
    wxScrolledWindow *m_scrolled_panel;
    wxBoxSizer *m_main_sizer;
    SidebarTabBar *m_tab_bar{nullptr};
    bool m_tabbed_mode{true};

    // Collapsible sections
    CollapsibleSection *m_printer_section;
    CollapsibleSection *m_filament_section;
    CollapsibleSection *m_process_section;
    CollapsibleSection *m_objects_section;

    // Compact labels shown on Objects tab in tabbed mode (hidden in unified mode)
    wxStaticText *m_print_pinned_label{nullptr};
    wxStaticText *m_printer_pinned_label{nullptr};
    wxStaticText *m_nozzle_pinned_label{nullptr};
    wxStaticText *m_nozzle_unified_label{nullptr}; // Non-bold label in filament sizer (rebuilt dynamically)

    // Section contents
    wxPanel *m_printer_content;
    PrinterSettingsPanel *m_printer_settings_panel;
    wxPanel *m_filament_content;
    FilamentSettingsPanel *m_filament_settings_panel;
    ProcessSection *m_process_content;
    wxPanel *m_objects_content;

    // Preset combos
    PlaterPresetComboBox *m_combo_printer;

    // Printer section nozzle diameter spins and filament combos (for quick extruder settings)
    std::vector<wxStaticBitmap *> m_printer_nozzle_lock_icons;
    std::vector<wxStaticBitmap *> m_printer_nozzle_undo_icons;
    std::vector<double> m_printer_nozzle_original_values;
    std::vector<::SpinInputDouble *> m_printer_nozzle_spins;
    std::vector<PlaterPresetComboBox *> m_printer_filament_combos;
    wxBoxSizer *m_printer_filament_sizer{nullptr};
    PlaterPresetComboBox *m_combo_print;
    std::vector<PlaterPresetComboBox *> m_combos_filament;
    wxBoxSizer *m_filaments_sizer;

    // Save buttons for preset sections
    ScalableButton *m_btn_save_printer;
    ScalableButton *m_btn_edit_physical_printer;
    ScalableButton *m_btn_save_filament;
    ScalableButton *m_btn_save_print;

    // Object components (reusing existing implementations)
    ObjectList *m_object_list;
    ObjectManipulation *m_object_manipulation;
    ObjectSettings *m_object_settings;
    ObjectLayers *m_object_layers;

    // Info display
    SlicedInfo *m_sliced_info;

    // Bottom button bar (pinned below the scroll area, shared by both views)
    wxPanel *m_buttons_panel{nullptr};
    ::SwitchButton *m_view_mode_switch{nullptr}; // Accordion / Tabbed view toggle
    ::SwitchButton *m_edit_mode_switch{nullptr}; // Pinned Settings / Edit Visibility toggle
    ScalableButton *m_btn_reslice;
    ScalableButton *m_btn_export_gcode;
    ScalableButton *m_btn_send_gcode;
    ScalableButton *m_btn_connect_gcode;
    ScalableButton *m_btn_export_gcode_removable;

    // State

    // Section state persistence
    std::map<wxString, bool> m_section_states;
};

// The sidebar's theming pass over a widget tree: every label, panel, box, icon, checkbox and
// input takes the sidebar's colours for the current theme. The panels under the object list run
// it over their content after building it and on a theme change.
void ApplySidebarTheme(wxWindow *root);

// Commit a filament color straight into the named filament preset, from any of the color pickers
// (sidebar swatch, sidebar filament panel, filament settings tab). The saved baseline and, when
// the preset is open in the edit slot, the working copy receive the same value, and user presets
// are written to disk immediately. A color change therefore never creates an unsaved-changes
// state, and pending edits of other options keep their dirty state untouched.
void commit_filament_color_to_preset(std::string preset_name, std::string color_hex);

} // namespace DSKY
