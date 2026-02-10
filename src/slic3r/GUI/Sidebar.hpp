///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_Sidebar_hpp_
#define slic3r_Sidebar_hpp_

#include "libslic3r/Preset.hpp"
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

namespace Slic3r
{

class DynamicPrintConfig;
class Preset;
class ModelObject;

namespace GUI
{

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
class ObjectInfo;
class SlicedInfo;
// ScalableButton is defined in wxExtensions.hpp
class ConfigOptionsGroup;
class FreqChangedParams;
class Tab;

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
    virtual ~TabbedSettingsPanel() = default;

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

    // Update visibility of rows, groups, and sections without rebuilding
    void UpdateSidebarVisibility();

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

    // Optional: called after content is built to set initial enable/disable state of dependent options
    virtual void ApplyToggleLogic() {}

    // Optional: returns whether a tab should be visible based on sidebar visibility settings
    // Subclasses override to check if any settings in the tab are visible
    virtual bool IsTabVisible(int tab_index) const { return true; }

    // Optional: called before content is destroyed during RebuildContent()
    // Subclasses should override to clear their m_setting_controls map
    virtual void ClearSettingControls() {}

    // Show/hide rows based on sidebar_visibility config (subclasses override to iterate m_setting_controls)
    virtual void UpdateRowVisibility() {}

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
        wxString tooltip;
        const ConfigOptionDef *opt_def{nullptr};
    };

    // Creates the common row UI elements (icons, label, sizers)
    // Returns empty context (row_sizer==nullptr) if opt_key not found in config
    RowUIContext CreateRowUIBase(wxWindow *parent, const std::string &opt_key, const wxString &label);

    // Binds undo icon click handler to revert the setting value
    void BindUndoHandler(wxStaticBitmap *undo_icon, const std::string &opt_key,
                         std::function<void(const std::string &)> on_setting_changed);

    // Called by derived class constructors after vtable is set up
    void BuildUI();

private:
    void EnsureContentBuilt(int index);
    void UpdateSizerProportions();

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

    // Override base class
    void msw_rescale() override;
    void sys_color_changed() override;

protected:
    // TabbedSettingsPanel interface
    std::vector<TabDefinition> GetTabDefinitions() override;
    wxPanel *BuildTabContent(int tab_index) override;
    void OnSysColorChanged() override;
    bool IsTabVisible(int tab_index) const override;
    void ClearSettingControls() override { m_setting_controls.clear(); }

    // Config access (implements TabbedSettingsPanel abstract methods)
    DynamicPrintConfig &GetEditedConfig() override;
    const DynamicPrintConfig &GetEditedConfig() const override;
    const Preset *GetSystemPresetParent() const override;
    Tab *GetSyncTab() const override;
    Preset::Type GetPresetType() const override { return Preset::TYPE_PRINT; }

private:
    // Tab indices for type safety
    enum TabIndex
    {
        TAB_LAYERS = 0,
        TAB_INFILL,
        TAB_SKIRT_BRIM,
        TAB_SUPPORT,
        TAB_SPEED,
        TAB_EXTRUDERS,
        TAB_ADVANCED,
        TAB_OUTPUT,
        TAB_COUNT
    };

    // Content builders - create the content panel for each tab
    wxPanel *BuildLayersContent();
    wxPanel *BuildInfillContent();
    wxPanel *BuildSkirtBrimContent();
    wxPanel *BuildSupportContent();
    wxPanel *BuildSpeedContent();
    wxPanel *BuildExtrudersContent();
    wxPanel *BuildAdvancedContent();
    wxPanel *BuildOutputContent();

    // Setting row helpers
    void CreateSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                          bool full_width = false);
    void CreateMultilineSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                                   int num_lines = 5);
    void OnSettingChanged(const std::string &opt_key);
    void UpdateUndoUI(const std::string &opt_key);
    void ApplyToggleLogic();
    void ToggleOption(const std::string &opt_key, bool enable);

    // Per-setting UI elements
    struct SettingUIElements
    {
        wxWindow *control{nullptr};
        wxWindow *lock_icon{nullptr};
        wxWindow *undo_icon{nullptr};
        wxWindow *label_text{nullptr};
        std::string original_value;
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
    bool IsTabVisible(int tab_index) const override;
    void ClearSettingControls() override
    {
        m_setting_controls.clear();
        m_marlin_limits_panel = nullptr;
        m_rrf_limits_panel = nullptr;
        m_stealth_mode_note = nullptr;
    }

    // Config access (implements TabbedSettingsPanel abstract methods)
    DynamicPrintConfig &GetEditedConfig() override;
    const DynamicPrintConfig &GetEditedConfig() const override;
    const Preset *GetSystemPresetParent() const override;
    Tab *GetSyncTab() const override;
    Preset::Type GetPresetType() const override { return Preset::TYPE_PRINTER; }

private:
    // Content builders
    wxPanel *BuildGeneralContent();
    wxPanel *BuildMachineLimitsContent();
    wxPanel *BuildSingleExtruderMMContent();
    wxPanel *BuildExtruderContent(size_t extruder_idx);

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
    void ApplyToggleLogic();
    void ToggleOption(const std::string &opt_key, bool enable);
    void ToggleExtruderOption(const std::string &opt_key, size_t extruder_idx, bool enable);

    void CreateExtruderSettingRow(wxWindow *parent, wxSizer *sizer, const std::string &opt_key, const wxString &label,
                                  size_t extruder_idx);
    void OnExtruderSettingChanged(const std::string &opt_key, size_t extruder_idx);

    // Machine limits sub-panels (show/hide based on gcode_flavor)
    wxPanel *m_marlin_limits_panel{nullptr};
    wxPanel *m_rrf_limits_panel{nullptr};
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

    // Override base class
    void msw_rescale() override;
    void sys_color_changed() override;

protected:
    // TabbedSettingsPanel interface
    std::vector<TabDefinition> GetTabDefinitions() override;
    wxPanel *BuildTabContent(int tab_index) override;
    void OnSysColorChanged() override;
    bool IsTabVisible(int tab_index) const override;
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
    // Tab indices for type safety
    enum TabIndex
    {
        TAB_FILAMENT = 0,
        TAB_COOLING,
        TAB_ADVANCED,
        TAB_OVERRIDES,
        TAB_COUNT
    };

    // Content builders
    wxPanel *BuildFilamentContent();
    wxPanel *BuildCoolingContent();
    wxPanel *BuildAdvancedContent();
    wxPanel *BuildOverridesContent();

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
    void ApplyToggleLogic();
    void ToggleOption(const std::string &opt_key, bool enable);

    // Per-setting UI elements
    struct SettingUIElements
    {
        wxWindow *control{nullptr};
        wxWindow *lock_icon{nullptr};
        wxWindow *undo_icon{nullptr};
        wxWindow *label_text{nullptr};
        ::CheckBox *enable_checkbox{nullptr}; // For nullable options
        std::string original_value;
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

    void SetPresetComboBox(PlaterPresetComboBox *combo);
    void UpdateFromConfig();
    void RebuildContent();
    void UpdateSidebarVisibility();

    void msw_rescale();
    void sys_color_changed();

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

    void show_info_sizer(bool show = true);
    void show_sliced_info_sizer(bool show);
    void show_btns_sizer(bool show);

    // When object settings are shown, minimize ObjectList to save space
    void set_object_settings_mode(bool settings_visible);
    void show_bulk_btns_sizer(bool show);
    void update_sliced_info_sizer();

    // Compatibility methods (may return nullptr if not applicable)
    ConfigOptionsGroup *og_freq_chng_params(bool is_fff);
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

    // Scaling
    void msw_rescale();
    void sys_color_changed();

    // Section state persistence
    void SaveSectionStates();
    void LoadSectionStates();

    // Rebuild settings panels (call when sidebar visibility settings change)
    void rebuild_settings_panels();

    // Update sidebar visibility without rebuilding (show/hide rows, groups, sections in-place)
    void update_sidebar_visibility();

    // Refresh the sidebar settings panel for the given preset type (Tab -> Sidebar sync)
    void refresh_settings_panel(Preset::Type type);

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

    Plater *m_plater;

    // Main layout
    wxScrolledWindow *m_scrolled_panel;
    wxBoxSizer *m_main_sizer;

    // Collapsible sections
    CollapsibleSection *m_printer_section;
    CollapsibleSection *m_filament_section;
    CollapsibleSection *m_process_section;
    CollapsibleSection *m_objects_section;

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
    ObjectInfo *m_object_info;
    SlicedInfo *m_sliced_info;

    // Action buttons
    wxPanel *m_buttons_panel;
    ScalableButton *m_btn_reslice;
    ScalableButton *m_btn_export_gcode;
    ScalableButton *m_btn_send_gcode;
    ScalableButton *m_btn_connect_gcode;
    ScalableButton *m_btn_export_gcode_removable;

    // State

    // Section state persistence
    std::map<wxString, bool> m_section_states;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_Sidebar_hpp_
