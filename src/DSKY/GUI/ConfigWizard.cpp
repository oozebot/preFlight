///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2012 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ConfigWizard_private.hpp"
#include "Theme.hpp"
#include "Widgets/UIColors.hpp"

#include <wx/hyperlink.h>
#include <wx/progdlg.h>

#include <algorithm>
#include <numeric>
#include <sstream>
#include <utility>
#include <unordered_map>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/dcclient.h>
#include <wx/statbmp.h>
#include <wx/checkbox.h>
#include <wx/statline.h>
#include <wx/dataview.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/display.h>
#include <wx/filefn.h>
#include <wx/wupdlock.h>
#include <wx/debug.h>

#ifdef WIN32
#include <wx/msw/registry.h>
#include <KnownFolders.h>
#include <Shlobj_core.h>
#endif // WIN32

#ifdef _WIN32
#define _MSW_DARK_MODE
#include "DarkMode.hpp"
#endif

#include "luminary/platform/host/Platform.hpp"
#include "luminary/platform/files/FileIO.hpp"
#include "luminary/platform/paths/Paths.hpp"
#include "luminary/config/model/Config.hpp"
#include "luminary/core/Prelude.hpp"
#include "luminary/model/scene/Model.hpp"
#include "luminary/colour/rgb/Color.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "Field.hpp"
#include "DesktopIntegrationDialog.hpp"
#include "DSKY/Config/Snapshot.hpp"
#include "format.hpp"
#include "MsgDialog.hpp"
#include "UnsavedChangesDialog.hpp"
#include "UpdatesUIManager.hpp"
#include "Plater.hpp"
#include "DSKY/Utils/AppUpdater.hpp"
#include "DSKY/GUI/I18N.hpp"
#include "DSKY/Config/Version.hpp"
#include "luminary/io/zip/miniz_extension.hpp"

namespace DSKY
{
using namespace Luminary;

using Config::Snapshot;
using Config::SnapshotDB;

bool Bundle::load(fs::path source_path, BundleLocation location, bool ais_preflight_bundle)
{
    this->preset_bundle = std::make_unique<PresetBundle>();
    this->location = location;
    this->is_preflight_bundle = ais_preflight_bundle;

    std::string path_string = source_path.string();
    // Throw when parsing invalid configuration. Only valid configuration is supposed to be provided over the air.
    auto [config_substitutions,
          presets_loaded] = preset_bundle->load_configbundle(path_string,
                                                             PresetBundle::LoadConfigBundleAttribute::LoadSystem,
                                                             ForwardCompatibilitySubstitutionRule::EnableSilent);
    UNUSED(config_substitutions);
    auto first_vendor = preset_bundle->vendors.begin();
    if (first_vendor == preset_bundle->vendors.end())
    {
        BOOST_LOG_TRIVIAL(error) << boost::format(
                                        "Vendor bundle: `%1%`: No vendor information defined, cannot install.") %
                                        path_string;
        return false;
    }
    if (presets_loaded == 0)
    {
        BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No profile loaded.") % path_string;
        return false;
    }

    BOOST_LOG_TRIVIAL(trace) << boost::format("Vendor bundle: `%1%`: %2% profiles loaded.") % path_string %
                                    presets_loaded;
    this->vendor_profile = &first_vendor->second;

    // Make all presets editable (no locked system presets)
    for (auto &preset : preset_bundle->prints)
        preset.is_system = false;
    for (auto &preset : preset_bundle->filaments)
        preset.is_system = false;
    for (auto &preset : preset_bundle->printers)
        preset.is_system = false;

    return true;
}

// Configuration data structures extensions needed for the wizard
Bundle::Bundle(Bundle &&other)
    : preset_bundle(std::move(other.preset_bundle))
    , vendor_profile(other.vendor_profile)
    , location(other.location)
    , is_preflight_bundle(other.is_preflight_bundle)
{
    other.vendor_profile = nullptr;
}

BundleMap BundleMap::load()
{
    BundleMap res;

    // Load previously-downloaded vendor profiles from data_dir()/vendor/
    const auto vendor_dir = (boost::filesystem::path(Luminary::data_dir()) / "vendor").make_preferred();
    if (fs::exists(vendor_dir))
    {
        for (const auto &dir_entry : boost::filesystem::directory_iterator(vendor_dir))
        {
            if (!Luminary::is_ini_file(dir_entry))
                continue;

            std::string id = dir_entry.path().stem().string();
            if (res.find(id) != res.end())
                continue;

            // Filaments.ini is loaded separately, not as a vendor bundle
            if (id == "Filaments")
                continue;

            Bundle bundle;
            if (bundle.load(dir_entry.path(), BundleLocation::IN_VENDOR))
            {
                BOOST_LOG_TRIVIAL(info) << "Loaded cached vendor profile: " << id;
                res.emplace(std::move(id), std::move(bundle));
            }
        }
    }

    return res;
}

// Return empty bundle if preFlight bundle not found
Bundle &BundleMap::preflight_bundle()
{
    static Bundle empty_bundle;
    auto it = find(PresetBundle::PREFLIGHT_BUNDLE);
    if (it == end())
    {
        return empty_bundle;
    }

    return it->second;
}

const Bundle &BundleMap::preflight_bundle() const
{
    return const_cast<BundleMap *>(this)->preflight_bundle();
}

// Helper to check if preFlight bundle exists
bool BundleMap::has_preflight_bundle() const
{
    return find(PresetBundle::PREFLIGHT_BUNDLE) != end();
}

// Printer model picker GUI control

struct PrinterPickerEvent : public wxEvent
{
    std::string vendor_id;
    std::string model_id;
    std::string variant_name;
    bool enable;

    PrinterPickerEvent(wxEventType eventType, int winid, std::string vendor_id, std::string model_id,
                       std::string variant_name, bool enable)
        : wxEvent(winid, eventType)
        , vendor_id(std::move(vendor_id))
        , model_id(std::move(model_id))
        , variant_name(std::move(variant_name))
        , enable(enable)
    {
    }

    virtual wxEvent *Clone() const { return new PrinterPickerEvent(*this); }
};

wxDEFINE_EVENT(EVT_PRINTER_PICK, PrinterPickerEvent);

const std::string PrinterPicker::PRINTER_PLACEHOLDER = "printer_placeholder.png";

PrinterPicker::PrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols,
                             const AppConfig &appconfig, const ModelFilter &filter)
    : wxPanel(parent), vendor_id(vendor.id), vendor_repo_id(vendor.repo_id), width(0)
{
    wxGetApp().UpdateDarkUI(this);
    const auto &models = vendor.models;

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    const auto font_title = GetFont().MakeBold().Scaled(1.3f);
    const auto font_name = GetFont().MakeBold();
    const auto font_alt_nozzle = GetFont().Scaled(0.9f);

    // wxGrid appends widgets by rows, but we need to construct them in columns.
    // These vectors are used to hold the elements so that they can be appended in the right order.
    std::vector<wxStaticText *> titles;
    std::vector<wxStaticBitmap *> bitmaps;
    std::vector<wxPanel *> variants_panels;

    int max_row_width = 0;
    int current_row_width = 0;

    bool is_variants = false;

    const fs::path vendor_dir_path = (fs::path(Luminary::data_dir()) / "vendor").make_preferred();
    const fs::path cache_dir_path = (fs::path(Luminary::data_dir()) / "cache").make_preferred();

    for (const auto &model : models)
    {
        if (!filter(model))
        {
            continue;
        }

        wxBitmap bitmap;
        int bitmap_width = 0;
        auto load_bitmap = [](const wxString &bitmap_file, wxBitmap &bitmap, int &bitmap_width)
        {
            bitmap.LoadFile(bitmap_file, wxBITMAP_TYPE_PNG);
            bitmap_width = bitmap.GetWidth();
        };

        bool found = false;
        for (const fs::path &res :
             {vendor_dir_path / vendor.id / model.thumbnail, cache_dir_path / vendor.id / model.thumbnail})
        {
            if (!fs::exists(res))
                continue;
            load_bitmap(DSKY::from_u8(res.string()), bitmap, bitmap_width);
            found = true;
            break;
        }

        if (!found)
        {
            BOOST_LOG_TRIVIAL(warning)
                << boost::format(
                       "Can't find bitmap file `%1%` for vendor `%2%`, printer `%3%`, using placeholder icon instead") %
                       model.thumbnail % vendor.id % model.id;
            load_bitmap(DSKY::from_u8(Luminary::var(PRINTER_PLACEHOLDER)), bitmap, bitmap_width);
        }

        wxStaticText *title = new wxStaticText(this, wxID_ANY, from_u8(model.name), wxDefaultPosition, wxDefaultSize,
                                               wxALIGN_LEFT);
        title->SetFont(font_name);
        const int wrap_width = std::max((int) GetScaledModelMinWrap(), bitmap_width);
        title->Wrap(wrap_width);

        current_row_width += wrap_width;
        if (titles.size() % max_cols == max_cols - 1)
        {
            max_row_width = std::max(max_row_width, current_row_width);
            current_row_width = 0;
        }

        titles.push_back(title);

        wxStaticBitmap *bitmap_widget = new wxStaticBitmap(this, wxID_ANY, bitmap);
        bitmaps.push_back(bitmap_widget);

        auto *variants_panel = new wxPanel(this);
        wxGetApp().UpdateDarkUI(variants_panel);
        auto *variants_sizer = new wxBoxSizer(wxVERTICAL);
        variants_panel->SetSizer(variants_sizer);
        const auto model_id = model.id;

        for (size_t i = 0; i < model.variants.size(); i++)
        {
            const auto &variant = model.variants[i];

            const auto label = format_wxstr("%1% %2% %3%", variant.name, _L("mm"), _L("nozzle"));

            if (i == 1)
            {
                auto *alt_label = new wxStaticText(variants_panel, wxID_ANY, _L("Alternate nozzles:"));
                alt_label->SetFont(font_alt_nozzle);
                int em = wxGetApp().em_unit();
                variants_sizer->Add(alt_label, 0, wxBOTTOM, em / 3);
                is_variants = true;
            }

            Checkbox *cbox = new Checkbox(variants_panel, label, model_id, variant.name);
            i == 0 ? cboxes.push_back(cbox) : cboxes_alt.push_back(cbox);

            const bool enabled = appconfig.get_variant(vendor.id, model_id, variant.name);
            cbox->SetValue(enabled);

            variants_sizer->Add(cbox, 0, wxBOTTOM, wxGetApp().em_unit() / 3);

            cbox->Bind(wxEVT_CHECKBOX, [this, cbox](wxCommandEvent &event) { on_checkbox(cbox, event.IsChecked()); });
        }

        variants_panels.push_back(variants_panel);
    }

    width = std::max(max_row_width, current_row_width);

    const size_t cols = std::min(max_cols, titles.size());

    auto *printer_grid = new wxFlexGridSizer(cols, 0, 2 * wxGetApp().em_unit());
    printer_grid->SetFlexibleDirection(wxVERTICAL | wxHORIZONTAL);

    if (titles.size() > 0)
    {
        const size_t odd_items = titles.size() % cols;

        for (size_t i = 0; i < titles.size() - odd_items; i += cols)
        {
            for (size_t j = i; j < i + cols; j++)
            {
                printer_grid->Add(bitmaps[j], 0, wxBOTTOM, 2 * wxGetApp().em_unit());
            }
            for (size_t j = i; j < i + cols; j++)
            {
                printer_grid->Add(titles[j], 0, wxBOTTOM, wxGetApp().em_unit() / 3);
            }
            for (size_t j = i; j < i + cols; j++)
            {
                printer_grid->Add(variants_panels[j]);
            }

            // Add separator space to multiliners
            if (titles.size() > cols)
            {
                for (size_t j = i; j < i + cols; j++)
                {
                    printer_grid->Add(1, 3 * wxGetApp().em_unit());
                }
            }
        }
        if (odd_items > 0)
        {
            const size_t rem = titles.size() - odd_items;

            for (size_t i = rem; i < titles.size(); i++)
            {
                printer_grid->Add(bitmaps[i], 0, wxBOTTOM, 2 * wxGetApp().em_unit());
            }
            for (size_t i = 0; i < cols - odd_items; i++)
            {
                printer_grid->AddSpacer(1);
            }
            for (size_t i = rem; i < titles.size(); i++)
            {
                printer_grid->Add(titles[i], 0, wxBOTTOM, wxGetApp().em_unit() / 3);
            }
            for (size_t i = 0; i < cols - odd_items; i++)
            {
                printer_grid->AddSpacer(1);
            }
            for (size_t i = rem; i < titles.size(); i++)
            {
                printer_grid->Add(variants_panels[i]);
            }
        }
    }

    auto *title_sizer = new wxBoxSizer(wxHORIZONTAL);
    if (!title.IsEmpty())
    {
        auto *title_widget = new wxStaticText(this, wxID_ANY, title);
        title_widget->SetFont(font_title);
        title_sizer->Add(title_widget);
    }
    title_sizer->AddStretchSpacer();

    if (titles.size() > 1 || is_variants)
    {
        // It only makes sense to add the All / None buttons if there's multiple printers
        // All Standard button is added when there are more variants for at least one printer
        auto *sel_all_std = new wxButton(this, wxID_ANY, titles.size() > 1 ? _L("All standard") : _L("Standard"));
        auto *sel_all = new wxButton(this, wxID_ANY, _L("All"));
        auto *sel_none = new wxButton(this, wxID_ANY, _L("None"));
        if (is_variants)
            sel_all_std->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->select_all(true, false); });
        sel_all->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->select_all(true, true); });
        sel_none->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->select_all(false); });
        if (is_variants)
            title_sizer->Add(sel_all_std, 0, wxRIGHT, GetScaledBtnSpacing());
        title_sizer->Add(sel_all, 0, wxRIGHT, GetScaledBtnSpacing());
        title_sizer->Add(sel_none);

        wxGetApp().SetWindowVariantForButton(sel_all_std);
        wxGetApp().SetWindowVariantForButton(sel_all);
        wxGetApp().SetWindowVariantForButton(sel_none);

        wxGetApp().UpdateDarkUI(sel_all_std);
        wxGetApp().UpdateDarkUI(sel_all);
        wxGetApp().UpdateDarkUI(sel_none);

        // fill button indexes used later for buttons rescaling
        if (is_variants)
            m_button_indexes = {sel_all_std->GetId(), sel_all->GetId(), sel_none->GetId()};
        else
        {
            sel_all_std->Destroy();
            m_button_indexes = {sel_all->GetId(), sel_none->GetId()};
        }
    }

    sizer->Add(title_sizer, 0, wxEXPAND | wxBOTTOM, GetScaledBtnSpacing());
    sizer->Add(printer_grid);

    SetSizer(sizer);
}

PrinterPicker::PrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols,
                             const AppConfig &appconfig)
    : PrinterPicker(parent, vendor, std::move(title), max_cols, appconfig,
                    [](const VendorProfile::PrinterModel &) { return true; })
{
}

void PrinterPicker::select_all(bool select, bool alternates)
{
    for (const auto &cb : cboxes)
    {
        if (cb->GetValue() != select)
        {
            cb->SetValue(select);
            on_checkbox(cb, select);
        }
    }

    if (!select)
    {
        alternates = false;
    }

    for (const auto &cb : cboxes_alt)
    {
        if (cb->GetValue() != alternates)
        {
            cb->SetValue(alternates);
            on_checkbox(cb, alternates);
        }
    }
}

void PrinterPicker::select_one(size_t i, bool select)
{
    if (i < cboxes.size() && cboxes[i]->GetValue() != select)
    {
        cboxes[i]->SetValue(select);
        on_checkbox(cboxes[i], select);
    }
}

bool PrinterPicker::any_selected() const
{
    for (const auto &cb : cboxes)
    {
        if (cb->GetValue())
        {
            return true;
        }
    }

    for (const auto &cb : cboxes_alt)
    {
        if (cb->GetValue())
        {
            return true;
        }
    }

    return false;
}

std::set<std::string> PrinterPicker::get_selected_models() const
{
    std::set<std::string> ret_set;

    for (const auto &cb : cboxes)
        if (cb->GetValue())
            ret_set.emplace(cb->model);

    for (const auto &cb : cboxes_alt)
        if (cb->GetValue())
            ret_set.emplace(cb->model);

    return ret_set;
}

void PrinterPicker::on_checkbox(const Checkbox *cbox, bool checked)
{
    PrinterPickerEvent evt(EVT_PRINTER_PICK, GetId(), vendor_id, cbox->model, cbox->variant, checked);
    AddPendingEvent(evt);
}

// Wizard page base

ConfigWizardPage::ConfigWizardPage(ConfigWizard *parent, wxString title, wxString shortname, unsigned indent)
    : wxPanel(parent->p->hscroll->GetContentPanel()), parent(parent), shortname(std::move(shortname)), indent(indent)
{
    wxGetApp().UpdateDarkUI(this);

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto *text = new wxStaticText(this, wxID_ANY, std::move(title), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    const auto font = GetFont().MakeBold().Scaled(1.5);
    text->SetFont(font);
    sizer->Add(text, 0, wxALIGN_LEFT, 0);
    sizer->AddSpacer(wxGetApp().em_unit());

    content = new wxBoxSizer(wxVERTICAL);
    sizer->Add(content, 1, wxEXPAND);

    SetSizer(sizer);

    this->Hide();

    Bind(wxEVT_SIZE,
         [this](wxSizeEvent &event)
         {
             this->Layout();
             event.Skip();
         });
}

ConfigWizardPage::~ConfigWizardPage() {}

wxStaticText *ConfigWizardPage::append_text(wxString text)
{
    auto *widget = new wxStaticText(this, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    widget->Wrap(GetScaledWrapWidth());
    widget->SetMinSize(wxSize(GetScaledWrapWidth(), -1));
    append(widget);
    return widget;
}

void ConfigWizardPage::append_spacer(int space)
{
    content->AddSpacer(space);
}

// Wizard pages

PageWelcome::PageWelcome(ConfigWizard *parent)
    : ConfigWizardPage(parent,
                       format_wxstr(
#ifdef __APPLE__
                           _L("Welcome to the %s Configuration Assistant")
#else
                           _L("Welcome to the %s Configuration Wizard")
#endif
                               ,
                           PREFLIGHT_APP_NAME),
                       _L("Welcome"))
    , welcome_text(append_text(format_wxstr(
          _L("Hello, welcome to %s! This %s helps you with the initial configuration; just a few settings and you will be ready to print."),
          PREFLIGHT_APP_NAME, _(ConfigWizard::name()))))
    , cbox_reset(append(new ::CheckBox(this, _L("Remove user profiles (a snapshot will be taken beforehand)"))))
    , cbox_integrate(append(
          new ::CheckBox(this, _L("Perform desktop integration (Sets this binary to be searchable by the system)."))))
{
    welcome_text->Hide();
    cbox_reset->Hide();
    cbox_integrate->Hide();
}

void PageWelcome::set_run_reason(ConfigWizard::RunReason run_reason)
{
    const bool data_empty = run_reason == ConfigWizard::RR_DATA_EMPTY;
    welcome_text->Show(data_empty);
    cbox_reset->Show(!data_empty);
#if defined(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
    if (!DesktopIntegrationDialog::is_integrated())
        cbox_integrate->Show(true);
    else
        cbox_integrate->Hide();
#else
    cbox_integrate->Hide();
#endif
}

PageUpdateManager::PageUpdateManager(ConfigWizard *parent_in)
    : ConfigWizardPage(parent_in, _L("Choose Vendors"), _L("Choose Vendors"))
{
    this->SetFont(wxGetApp().normal_font());
    const int em = em_unit(this);

    auto *intro = new wxStaticText(this, wxID_ANY,
                                   _L("Select the printer vendors you use. Printer and filament profiles will be "
                                      "installed for the vendors you select."));
    intro->Wrap(70 * em);
    append(intro, 0, wxBOTTOM, em);

    vendor_scroll = new ScrollablePanel(this);
    vendor_scroll->SetMinSize(wxSize(-1, 30 * em));

    append(vendor_scroll, 1, wxEXPAND);

    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *sel_all = new wxButton(this, wxID_ANY, _L("All"));
    auto *sel_none = new wxButton(this, wxID_ANY, _L("None"));
    wxGetApp().UpdateDarkUI(sel_all);
    wxGetApp().UpdateDarkUI(sel_none);
    wxGetApp().SetWindowVariantForButton(sel_all);
    wxGetApp().SetWindowVariantForButton(sel_none);
    btn_sizer->Add(sel_all, 0, wxRIGHT, em / 2);
    btn_sizer->Add(sel_none);
    append(btn_sizer, 0, wxTOP, em / 2);

    sel_all->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent &)
                  {
                      for (auto &[id, cb] : vendor_checkboxes)
                          cb->SetValue(true);
                  });
    sel_none->Bind(wxEVT_BUTTON,
                   [this](wxCommandEvent &)
                   {
                       for (auto &[id, cb] : vendor_checkboxes)
                           cb->SetValue(false);
                   });

    append_spacer(em);

    auto *local_label = new wxStaticText(this, wxID_ANY,
                                         _L("Have a vendor profile bundle? Load it here to add it to the list above."));
    local_label->Wrap(70 * em);
    append(local_label, 0, wxBOTTOM, em / 2);

    auto *btn_load_local = new wxButton(this, wxID_ANY, _L("Load Local Profile..."));
    wxGetApp().UpdateDarkUI(btn_load_local);
    wxGetApp().SetWindowVariantForButton(btn_load_local);
    append(btn_load_local);

    btn_load_local->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { import_local_vendor_bundle(); });

    this->Bind(wxEVT_SHOW,
               [this, parent_in](wxShowEvent &evt)
               {
                   if (evt.IsShown())
                   {
                       is_active = true;
                   }
                   else if (is_active && parent_in->IsShown())
                   {
                       // Leaving the page - collect selected vendors and proceed
                       auto *priv = wizard_p();
                       priv->selected_vendor_ids.clear();
                       std::string saved;
                       for (const auto &[id, cb] : vendor_checkboxes)
                       {
                           if (cb->GetValue())
                           {
                               priv->selected_vendor_ids.insert(id);
                               if (!saved.empty())
                                   saved += ";";
                               saved += id;
                           }
                       }
                       // Persist selected vendors so they're restored next time
                       wxGetApp().app_config->set("selected_vendors", saved);

                       priv->is_config_from_archive = true;
                       priv->set_config_updated_from_archive(true, !priv->selected_vendor_ids.empty());

                       is_active = false;
                   }
               });
}

void PageUpdateManager::populate_vendor_list()
{
    vendor_checkboxes.clear();
    vendor_scroll->GetContentPanel()->DestroyChildren();

    const int em = em_unit(this);
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    // Sort vendors alphabetically by display name
    struct VendorEntry
    {
        std::string id;
        std::string name;
    };
    std::vector<VendorEntry> sorted_vendors;
    for (const auto &[id, bundle] : wizard_p()->bundles)
    {
        if (bundle.vendor_profile)
            sorted_vendors.push_back({id, bundle.vendor_profile->name});
    }
    std::sort(sorted_vendors.begin(), sorted_vendors.end(), [](const VendorEntry &a, const VendorEntry &b)
              { return boost::algorithm::to_lower_copy(a.name) < boost::algorithm::to_lower_copy(b.name); });

    // Restore previously selected vendors from AppConfig
    std::set<std::string> saved_vendors;
    const std::string saved = wxGetApp().app_config->get("selected_vendors");
    if (!saved.empty())
    {
        std::istringstream ss(saved);
        std::string token;
        while (std::getline(ss, token, ';'))
            if (!token.empty())
                saved_vendors.insert(token);
    }

    for (const auto &entry : sorted_vendors)
    {
        auto *cb = new ::CheckBox(vendor_scroll->GetContentPanel(), from_u8(entry.name));
        if (saved_vendors.count(entry.id))
            cb->SetValue(true);
        sizer->Add(cb, 0, wxBOTTOM, em / 4);
        vendor_checkboxes[entry.id] = cb;
    }

    vendor_scroll->SetSizer(sizer);
    vendor_scroll->FitInside();
    vendor_scroll->Layout();
}

void PageUpdateManager::import_local_vendor_bundle()
{
    namespace fs = boost::filesystem;

    // Allowed resource file extensions
    static const std::set<std::string> allowed_extensions = {".ini", ".png", ".svg", ".stl", ".obj", ".idx"};

    // 100 MB per-entry extraction limit
    static constexpr size_t MAX_ENTRY_SIZE = 100 * 1024 * 1024;

    wxFileDialog dialog(this, _L("Select vendor profile bundle (ZIP)"), from_u8(wxGetApp().app_config->get_last_dir()),
                        "", file_wildcards(FT_ZIP), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    const std::string zip_path = into_u8(dialog.GetPath());
    wxGetApp().app_config->update_config_dir(fs::path(zip_path).parent_path().string());

    // RAII guard for the ZIP handle
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!open_zip_reader(&zip, zip_path))
    {
        show_error(this, _L("Failed to open ZIP file."));
        return;
    }
    auto zip_guard = [&zip]()
    {
        close_zip_reader(&zip);
    };
    struct ZipCloser
    {
        std::function<void()> fn;
        ~ZipCloser() { fn(); }
    } zip_closer{zip_guard};

    // Scan for the .ini file inside the ZIP
    std::string ini_entry_name;
    std::string ini_stem;
    int num_files = (int) mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < num_files; i++)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        std::string name = stat.m_filename;
        if (name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
            boost::algorithm::iends_with(name, ".ini"))
        {
            ini_entry_name = name;
            ini_stem = fs::path(name).stem().string();
            break;
        }
    }

    if (ini_entry_name.empty())
    {
        show_error(this, _L("No vendor .ini file found in the ZIP archive.\n\n"
                            "The ZIP should contain a vendor .ini file at the top level."));
        return;
    }

    // Build _local ID - strip any existing _local suffix first
    std::string local_id = ini_stem;
    while (boost::algorithm::iends_with(local_id, "_local"))
        local_id = local_id.substr(0, local_id.size() - 6);
    local_id += "_local";

    const fs::path vendor_dir = fs::path(Luminary::data_dir()) / "vendor";
    const fs::path local_ini_path = vendor_dir / (local_id + ".ini");
    const fs::path local_res_dir = vendor_dir / local_id;

    // Check for existing local profile
    if (fs::exists(local_ini_path))
    {
        MessageDialog overwrite_dlg(this,
                                    format_wxstr(_L("A local profile '%1%' already exists.\n\nOverwrite it?"),
                                                 local_id),
                                    _L("Profile Exists"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        if (overwrite_dlg.ShowModal() != wxID_YES)
            return;

        boost::system::error_code ec;
        fs::remove(local_ini_path, ec);
        if (fs::exists(local_res_dir))
            fs::remove_all(local_res_dir, ec);
    }

    // Extract files from ZIP
    fs::create_directories(local_res_dir);
    bool ini_extracted = false;

    for (int i = 0; i < num_files; i++)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        if (stat.m_is_directory)
            continue;

        // Size limit to prevent zip bombs
        if (stat.m_uncomp_size > MAX_ENTRY_SIZE)
        {
            BOOST_LOG_TRIVIAL(warning) << "Skipping oversized ZIP entry: " << stat.m_filename << " ("
                                       << stat.m_uncomp_size << " bytes)";
            continue;
        }
        if (stat.m_uncomp_size == 0)
            continue;

        std::string name = stat.m_filename;
        fs::path target;

        if (name == ini_entry_name)
        {
            target = local_ini_path;
        }
        else if (boost::algorithm::istarts_with(name, ini_stem + "/") ||
                 boost::algorithm::istarts_with(name, ini_stem + "\\"))
        {
            std::string rel = name.substr(ini_stem.size() + 1);
            if (rel.empty())
                continue;

            // Only extract known-safe file types
            std::string ext = fs::path(rel).extension().string();
            boost::algorithm::to_lower(ext);
            if (allowed_extensions.find(ext) == allowed_extensions.end())
            {
                BOOST_LOG_TRIVIAL(warning) << "Skipping disallowed file type in profile ZIP: " << name;
                continue;
            }

            target = local_res_dir / rel;

            // Path traversal protection - verify target stays inside resource dir
            fs::path canonical_base = fs::weakly_canonical(local_res_dir);
            fs::path canonical_target = fs::weakly_canonical(target);
            std::string base_str = canonical_base.string();
            std::string target_str = canonical_target.string();
            if (target_str.size() < base_str.size() || target_str.compare(0, base_str.size(), base_str) != 0)
            {
                BOOST_LOG_TRIVIAL(error) << "Path traversal blocked in profile ZIP: " << name;
                continue;
            }
        }
        else
        {
            continue;
        }

        size_t buf_size = (size_t) stat.m_uncomp_size;
        std::vector<char> buf(buf_size);
        if (!mz_zip_reader_extract_to_mem(&zip, i, buf.data(), buf_size, 0))
        {
            BOOST_LOG_TRIVIAL(error) << "Failed to extract " << name << " from local profile ZIP";
            continue;
        }

        fs::create_directories(target.parent_path());
        boost::nowide::ofstream ofs(target.string(), std::ios::binary | std::ios::trunc);
        if (ofs.is_open())
        {
            ofs.write(buf.data(), buf_size);
            ofs.close();
            if (name == ini_entry_name)
                ini_extracted = true;
        }
    }

    // ZIP is closed automatically by zip_closer

    if (!ini_extracted)
    {
        show_error(this, _L("Failed to extract vendor profile from ZIP."));
        boost::system::error_code ec;
        fs::remove_all(local_res_dir, ec);
        return;
    }

    // Patch the .ini: append " (local)" to display name, strip post_process keys
    try
    {
        std::string ini_content;
        {
            boost::nowide::ifstream ifs(local_ini_path.string());
            ini_content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        }

        std::istringstream iss(ini_content);
        std::ostringstream oss;
        std::string line;
        bool in_vendor_section = false;
        bool name_patched = false;
        while (std::getline(iss, line))
        {
            // Track sections
            std::string trimmed = boost::algorithm::trim_copy(line);
            if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']')
            {
                in_vendor_section = (trimmed == "[vendor]");
            }

            // Strip post_process keys from all sections
            if (boost::algorithm::istarts_with(trimmed, "post_process"))
            {
                size_t eq_pos = trimmed.find('=');
                if (eq_pos != std::string::npos)
                {
                    std::string key = trimmed.substr(0, eq_pos);
                    boost::algorithm::trim(key);
                    if (boost::algorithm::iequals(key, "post_process"))
                    {
                        BOOST_LOG_TRIVIAL(info) << "Stripped post_process from local profile for security";
                        continue;
                    }
                }
            }

            // Patch display name with "(local)" suffix
            if (in_vendor_section && !name_patched)
            {
                size_t eq_pos = trimmed.find('=');
                if (eq_pos != std::string::npos)
                {
                    std::string key = trimmed.substr(0, eq_pos);
                    boost::algorithm::trim(key);
                    if (key == "name")
                    {
                        std::string value = trimmed.substr(eq_pos + 1);
                        boost::algorithm::trim(value);
                        while (boost::algorithm::iends_with(value, " (local)"))
                            value = value.substr(0, value.size() - 8);
                        oss << "name = " << value << " (local)\n";
                        name_patched = true;
                        continue;
                    }
                }
            }

            oss << line << "\n";
        }

        boost::nowide::ofstream ofs(local_ini_path.string(), std::ios::trunc);
        ofs << oss.str();
    }
    catch (const std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << "Failed to patch local profile INI: " << e.what();
        show_error(this, _L("Failed to process the vendor profile."));
        boost::system::error_code ec;
        fs::remove(local_ini_path, ec);
        fs::remove_all(local_res_dir, ec);
        return;
    }

    // Load the new bundle into the wizard
    Bundle new_bundle;
    if (new_bundle.load(local_ini_path, BundleLocation::IN_VENDOR))
    {
        auto *priv = wizard_p();
        priv->bundles.erase(local_id);
        priv->bundles.emplace(local_id, std::move(new_bundle));
        populate_vendor_list();

        if (auto it = vendor_checkboxes.find(local_id); it != vendor_checkboxes.end())
            it->second->SetValue(true);

        BOOST_LOG_TRIVIAL(info) << "Loaded local vendor profile: " << local_id;
    }
    else
    {
        show_error(this, _L("Failed to load the vendor profile. The .ini file may be invalid."));
        boost::system::error_code ec;
        fs::remove(local_ini_path, ec);
        fs::remove_all(local_res_dir, ec);
    }
}

PagePrinters::PagePrinters(ConfigWizard *parent, wxString title, wxString shortname, const VendorProfile &vendor,
                           unsigned indent)
    : ConfigWizardPage(parent, std::move(title), std::move(shortname), indent)
    , install(false) // only used for 3rd party vendors
{
    enum
    {
        COL_SIZE = 200,
    };

    AppConfig *appconfig = &this->wizard_p()->appconfig_new;

    const auto families = vendor.families();
    for (const auto &family : families)
    {
        const auto filter = [&](const VendorProfile::PrinterModel &model)
        {
            return model.technology == ptFFF && model.family == family;
        };

        if (std::find_if(vendor.models.begin(), vendor.models.end(), filter) == vendor.models.end())
        {
            continue;
        }

        const auto picker_title = family.empty() ? wxString() : format_wxstr(_L("%s Family"), family);
        auto *picker = new PrinterPicker(this, vendor, picker_title, MAX_COLS, *appconfig, filter);

        picker->Bind(EVT_PRINTER_PICK,
                     [this, appconfig](const PrinterPickerEvent &evt)
                     {
                         appconfig->set_variant(evt.vendor_id, evt.model_id, evt.variant_name, evt.enable);
                         wizard_p()->on_printer_pick(this, evt);
                     });

        append(new StaticLine(this));

        append(picker);
        printer_pickers.push_back(picker);
        has_printers = true;
    }
}

void PagePrinters::select_all(bool select, bool alternates)
{
    for (auto picker : printer_pickers)
    {
        picker->select_all(select, alternates);
    }
}

void PagePrinters::unselect_all_presets()
{
    assert(!printer_pickers.empty());
    const std::string vendor_id = printer_pickers[0]->vendor_id;

    PresetBundle *preset_bundle{nullptr};
    for (const auto &[bundle_name, bundle] : wizard_p()->bundles)
    {
        if (bundle_name == vendor_id)
        {
            preset_bundle = bundle.preset_bundle.get();
            break;
        }
    }

    if (preset_bundle)
    {
        auto unselect =
            [preset_bundle](const std::string &vendor_id, const std::string &model, const std::string &variant)
        {
            for (auto &preset : preset_bundle->printers)
            {
                if (preset.config.opt_string("printer_model") == model &&
                    preset.config.opt_string("printer_variant") == variant)
                {
                    preset.is_visible = false;
                }
            }
        };

        // unselect presets in preset bundle, if related model and variant was checked in Picker
        for (auto picker : printer_pickers)
        {
            for (const auto &cb : picker->cboxes)
            {
                if (cb->GetValue())
                    unselect(picker->vendor_id, cb->model, cb->variant);
            }

            for (const auto &cb : picker->cboxes_alt)
            {
                if (cb->GetValue())
                    unselect(picker->vendor_id, cb->model, cb->variant);
            }
        }
    }

    // remove vendor from appconfig_new
    AppConfig *appconfig = &wizard_p()->appconfig_new;

    AppConfig::VendorMap new_vendors = appconfig->vendors();
    if (new_vendors.find(vendor_id) != new_vendors.end())
    {
        new_vendors.erase(vendor_id);
        appconfig->set_vendors(new_vendors);
    }
}

int PagePrinters::get_width() const
{
    return std::accumulate(printer_pickers.begin(), printer_pickers.end(), 0,
                           [](int acc, const PrinterPicker *picker) { return std::max(acc, picker->get_width()); });
}

bool PagePrinters::any_selected() const
{
    for (const auto *picker : printer_pickers)
    {
        if (picker->any_selected())
        {
            return true;
        }
    }

    return false;
}

std::set<std::string> PagePrinters::get_selected_models()
{
    std::set<std::string> ret_set;

    for (const auto *picker : printer_pickers)
    {
        std::set<std::string> tmp_models = picker->get_selected_models();
        ret_set.insert(tmp_models.begin(), tmp_models.end());
    }

    return ret_set;
}

void PagePrinters::set_run_reason(ConfigWizard::RunReason run_reason)
{
    if (is_primary_printer_page &&
        (run_reason == ConfigWizard::RR_DATA_EMPTY || run_reason == ConfigWizard::RR_DATA_LEGACY) &&
        printer_pickers.size() > 0 && printer_pickers[0]->vendor_id == PresetBundle::PREFLIGHT_BUNDLE)
    {
        printer_pickers[0]->select_one(0, true);
    }
}

const std::string PageMaterials::EMPTY;

PageMaterials::PageMaterials(ConfigWizard *parent, Materials *materials, wxString title, wxString shortname,
                             wxString list1name)
    : ConfigWizardPage(parent, std::move(title), std::move(shortname))
    , materials(materials)
    , list_printer(new StringList(this, wxLB_MULTIPLE))
    , list_type(new StringList(this))
    , list_vendor(new StringList(this))
    , list_profile(new PresetList(this))
{
    const int em = parent->em_unit();

    // Sort toggle: Type>Vendor vs Vendor>Type
    auto *toggle_sizer = new wxBoxSizer(wxHORIZONTAL);
    toggle_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Sort by:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    auto *btn_type_first = new wxButton(this, wxID_ANY, _L("Type > Vendor"), wxDefaultPosition, wxDefaultSize,
                                        wxBU_EXACTFIT);
    auto *btn_vendor_first = new wxButton(this, wxID_ANY, _L("Vendor > Type"), wxDefaultPosition, wxDefaultSize,
                                          wxBU_EXACTFIT);
    wxGetApp().SetWindowVariantForButton(btn_type_first);
    wxGetApp().SetWindowVariantForButton(btn_vendor_first);
    btn_type_first->SetWindowStyle(btn_type_first->GetWindowStyle() | wxNO_BORDER);
    btn_vendor_first->SetWindowStyle(btn_vendor_first->GetWindowStyle() | wxNO_BORDER);
    toggle_sizer->Add(btn_vendor_first, 0, wxRIGHT, em / 4);
    toggle_sizer->Add(btn_type_first);
    append(toggle_sizer, 0, wxBOTTOM, em / 4);

    auto *disclaimer =
        new wxStaticText(this, wxID_ANY,
                         _L("Filament profiles are community-sourced and may not reflect your specific setup. "
                            "Always verify settings before printing."));
    disclaimer->Wrap(65 * em);
    disclaimer->SetFont(wxGetApp().small_font());
    auto *vendor_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *vendor_text = new wxStaticText(this, wxID_ANY, _L("Vendors can submit corrections or updates via our "));
    vendor_text->SetFont(wxGetApp().small_font());
    auto *vendor_link = new wxGenericHyperlinkCtrl(this, wxID_ANY, _L("profiles repository"),
                                                   "https://github.com/oozebot/preFlight-profiles");
    vendor_link->SetFont(wxGetApp().small_font());
    wxGetApp().tint_hyperlink(vendor_link);
    vendor_sizer->Add(vendor_text, 0, wxALIGN_CENTER_VERTICAL);
    vendor_sizer->Add(vendor_link, 0, wxALIGN_CENTER_VERTICAL);
    append(disclaimer, 0, wxBOTTOM, em / 4);
    append(vendor_sizer, 0);

    auto *note = new wxStaticText(
        this, wxID_ANY,
        _L("Filaments are not bound to nozzle size and do not include a Max Volumetric Flow override. "
           "Adjust volumetric flow limits in your Printer profile to match your hotend's capabilities, "
           "and any filament-specific overrides in your Filament presets."));
    note->Wrap(65 * em);
    note->SetFont(wxGetApp().small_font());
    append(note, 0, wxBOTTOM, em / 2);

    // Style sort buttons ourselves (skip UpdateDarkUI - it binds hover/focus handlers that override colors)
    wxColour active_bg = UIColors::AccentDark(); // themed accent (darker variant)
    wxColour active_fg(*wxBLACK);
    wxColour inactive_bg = wxGetApp().get_window_default_clr();
    wxColour inactive_fg = wxGetApp().get_label_clr_default();
    auto style_sort_buttons = [=](wxButton *active, wxButton *inactive)
    {
        active->SetBackgroundColour(active_bg);
        active->SetForegroundColour(active_fg);
        inactive->SetBackgroundColour(inactive_bg);
        inactive->SetForegroundColour(inactive_fg);
        active->Refresh();
        inactive->Refresh();
    };
    style_sort_buttons(btn_vendor_first, btn_type_first);

    btn_type_first->Bind(wxEVT_BUTTON,
                         [this, btn_type_first, btn_vendor_first, style_sort_buttons](wxCommandEvent &)
                         {
                             if (vendor_first)
                             {
                                 vendor_first = false;
                                 col1_label->SetLabel(_L("Type:"));
                                 col2_label->SetLabel(_L("Vendor:"));
                                 reload_presets();
                                 style_sort_buttons(btn_type_first, btn_vendor_first);
                             }
                         });
    btn_vendor_first->Bind(wxEVT_BUTTON,
                           [this, btn_type_first, btn_vendor_first, style_sort_buttons](wxCommandEvent &)
                           {
                               if (!vendor_first)
                               {
                                   vendor_first = true;
                                   col1_label->SetLabel(_L("Vendor:"));
                                   col2_label->SetLabel(_L("Type:"));
                                   reload_presets();
                                   style_sort_buttons(btn_vendor_first, btn_type_first);
                               }
                           });

    const int list_h = 30 * em;

    // list_printer is kept for internal filtering logic but hidden from UI
    list_printer->Hide();

    list_type->SetMinSize(wxSize(12 * em, list_h));
    list_vendor->SetMinSize(wxSize(14 * em, list_h));
    list_profile->SetMinSize(wxSize(20 * em, list_h));

#ifndef _WIN32
    for (wxWindow *win : std::initializer_list<wxWindow *>{list_type, list_vendor, list_profile})
        win->SetBackgroundColour(wxGetApp().get_window_default_clr());
#endif

    // 4-column layout: Col1 | Col2 | Profile | Details (Details gets remaining space)
    grid = new wxFlexGridSizer(4, em / 2, em);
    grid->AddGrowableCol(3, 1);
    grid->AddGrowableRow(1, 1);

    col1_label = new wxStaticText(this, wxID_ANY, _L("Vendor:"));
    col2_label = new wxStaticText(this, wxID_ANY, _L("Type:"));
    grid->Add(col1_label);
    grid->Add(col2_label);
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Profile:")));
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Details:")));

    grid->Add(list_type, 0, wxEXPAND);
    grid->Add(list_vendor, 0, wxEXPAND);
    grid->Add(list_profile, 1, wxEXPAND);

    html_window = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(html_window);
#endif
    grid->Add(html_window, 1, wxEXPAND);

    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *sel_all = new wxButton(this, wxID_ANY, _L("All"));
    auto *sel_none = new wxButton(this, wxID_ANY, _L("None"));
    btn_sizer->Add(sel_all, 0, wxRIGHT, em / 2);
    btn_sizer->Add(sel_none);

    wxGetApp().UpdateDarkUI(list_type);
    wxGetApp().UpdateDarkUI(list_vendor);
    wxGetApp().UpdateDarkUI(list_profile);
    wxGetApp().UpdateDarkUI(sel_all);
    wxGetApp().UpdateDarkUI(sel_none);

    wxGetApp().SetWindowVariantForButton(sel_all);
    wxGetApp().SetWindowVariantForButton(sel_none);

    grid->Add(new wxBoxSizer(wxHORIZONTAL));
    grid->Add(new wxBoxSizer(wxHORIZONTAL));
    grid->Add(btn_sizer, 0, wxALIGN_RIGHT);
    grid->Add(new wxBoxSizer(wxHORIZONTAL));

    append(grid, 1, wxEXPAND);

    list_type->Bind(wxEVT_LISTBOX,
                    [this](wxCommandEvent &) { update_lists(list_type->GetSelection(), list_vendor->GetSelection()); });
    list_vendor->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &)
                      { update_lists(list_type->GetSelection(), list_vendor->GetSelection()); });

    list_profile->Bind(wxEVT_CHECKLISTBOX,
                       [this](wxCommandEvent &evt)
                       {
                           select_material(evt.GetInt());
                           on_material_highlighted(evt.GetInt());
                       });
    list_profile->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &evt) { on_material_highlighted(evt.GetInt()); });

    sel_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { select_all(true); });
    sel_none->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { select_all(false); });

    reload_presets();
    clear_compatible_printers_label();
}

void PageMaterials::check_and_update_presets(bool force_reload_presets /*= false*/)
{
    if (presets_loaded)
        return;
    wizard_p()->update_materials();
    //    if (force_reload_presets)
    reload_presets();
}

void PageMaterials::on_paint() {}
void PageMaterials::on_mouse_move_on_profiles(wxMouseEvent &evt)
{
    const wxClientDC dc(list_profile);
    const wxPoint pos = evt.GetLogicalPosition(dc);
    int item = list_profile->HitTest(pos);
    on_material_hovered(item);
}
void PageMaterials::on_mouse_enter_profiles(wxMouseEvent &evt) {}
void PageMaterials::on_mouse_leave_profiles(wxMouseEvent &evt)
{
    on_material_hovered(-1);
}
void PageMaterials::reload_presets()
{
    clear();

    // Always show all filaments regardless of printer - populate printer list with "(All)" selected
    list_printer->append(_L("(All)"), &EMPTY);
    list_printer->SetSelection(0);
    sel_printers_prev.Clear();
    sel_type_prev = wxNOT_FOUND;
    sel_vendor_prev = wxNOT_FOUND;
    update_lists(0, 0, 0);

    presets_loaded = true;
}

void PageMaterials::set_compatible_printers_html_window(const std::vector<std::string> &printer_names,
                                                        bool all_printers)
{
    const auto text_clr = wxGetApp().get_label_clr_default();
    const auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));
    const auto bgr_clr_str = wxGetApp().get_html_bg_color(parent);
    wxString text;
    {
        // TRN ConfigWizard: Materials : "%1%" = "Filaments"
        wxString first_line = format_wxstr(
            _L("%1% marked with <b>*</b> are <b>not</b> compatible with some installed printers."), _L("Filaments"));
        if (all_printers)
        {
            // TRN ConfigWizard: Materials : "%1%" = "filament"
            wxString second_line = format_wxstr(_L("All installed printers are compatible with the selected %1%."),
                                                _L("filament"));
            text = wxString::Format("<html>"
                                    "<style>"
                                    "table{border-spacing: 1px;}"
                                    "</style>"
                                    "<body bgcolor= %s>"
                                    "<font color=%s>"
                                    "<font size=\"3\">"
                                    "%s<br /><br />%s"
                                    "</font>"
                                    "</body>"
                                    "</html>",
                                    bgr_clr_str, text_clr_str, first_line, second_line);
        }
        else
        {
            wxString second_line;
            if (!printer_names.empty())
                second_line = _L("Only the following installed printers are compatible with the selected filaments") +
                              ":";
            text = wxString::Format("<html>"
                                    "<style>"
                                    "table{border-spacing: 1px;}"
                                    "</style>"
                                    "<body bgcolor= %s>"
                                    "<font color=%s>"
                                    "<font size=\"3\">"
                                    "%s<br /><br />%s"
                                    "<table>"
                                    "<tr>",
                                    bgr_clr_str, text_clr_str, first_line, second_line);
            for (size_t i = 0; i < printer_names.size(); ++i)
            {
                text += wxString::Format("<td>%s</td>", boost::nowide::widen(printer_names[i]));
                if (i % 3 == 2)
                {
                    text += wxString::Format("</tr>"
                                             "<tr>");
                }
            }
            text += wxString::Format("</tr>"
                                     "</table>"
                                     "</font>"
                                     "</body>"
                                     "</html>");
        }
    }

    wxFont font = wxGetApp().normal_font(); // get_default_font_for_dpi(this, get_dpi_for_window(this));
    const int fs = font.GetPointSize();
    int size[] = {fs, fs, fs, fs, fs, fs, fs};
    html_window->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
    html_window->SetPage(text);
}

void PageMaterials::clear_compatible_printers_label()
{
    last_hovered_item = -1;
    const auto bgr_clr_str = wxGetApp().get_html_bg_color(parent);
    const auto text_clr = wxGetApp().get_label_clr_default();
    const auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));

    wxString text = wxString::Format("<html><body bgcolor=%s><font color=%s><font size=\"3\">"
                                     "%s"
                                     "</font></body></html>",
                                     bgr_clr_str, text_clr_str, _L("Select a filament to see its settings."));

    wxFont font = wxGetApp().normal_font();
    const int fs = font.GetPointSize();
    int size[] = {fs, fs, fs, fs, fs, fs, fs};
    html_window->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
    html_window->SetPage(text);
}

void PageMaterials::on_material_hovered(int sel_material) {}

void PageMaterials::on_material_highlighted(int sel_material)
{
    if (sel_material == last_hovered_item)
        return;
    if (sel_material == -1)
    {
        clear_compatible_printers_label();
        return;
    }
    last_hovered_item = sel_material;

    std::string material_name = list_profile->get_data(sel_material);
    const std::vector<const Preset *> matching_materials = materials->get_presets_by_alias(material_name);
    if (matching_materials.empty())
    {
        clear_compatible_printers_label();
        return;
    }

    const Preset *preset = matching_materials.front();
    const auto &config = preset->config;

    const auto text_clr = wxGetApp().get_label_clr_default();
    const auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));
    const auto bgr_clr_str = wxGetApp().get_html_bg_color(parent);

    std::string display_name = preset->name;
    auto at_pos = display_name.find(" @");
    if (at_pos != std::string::npos)
        display_name = display_name.substr(0, at_pos);

    wxString html = wxString::Format("<html><body bgcolor=%s><font color=%s><font size=\"3\">"
                                     "<b>%s</b><br/><br/>"
                                     "<table cellspacing=\"1\">",
                                     bgr_clr_str, text_clr_str, from_u8(display_name));

    for (const std::string &key : config.keys())
    {
        const ConfigOption *opt = config.option(key);
        if (!opt)
            continue;
        std::string val = opt->serialize();
        if (val.empty() || val == "nil" || val == "0" || val == "\"\"")
            continue;
        // Skip internal/compatibility keys
        if (key == "compatible_printers_condition" || key == "compatible_printers" ||
            key == "compatible_prints_condition" || key == "compatible_prints" || key == "inherits" ||
            key == "filament_settings_id")
            continue;
        html += wxString::Format("<tr><td valign=\"top\"><b>%s&nbsp;&nbsp;</b></td><td valign=\"top\">%s</td></tr>",
                                 from_u8(key), from_u8(val));
    }

    html += "</table></font></body></html>";

    wxFont font = wxGetApp().normal_font();
    const int fs = font.GetPointSize();
    int size[] = {fs, fs, fs, fs, fs, fs, fs};
    html_window->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
    html_window->SetPage(html);
}

void PageMaterials::update_lists(int sel_type, int sel_vendor, int last_selected_printer /* = -1*/)
{
    wxWindowUpdateLocker freeze_guard(this);
    (void) freeze_guard;

    wxArrayInt sel_printers;
    int sel_printers_count = list_printer->GetSelections(sel_printers);

    // Does our wxWidgets version support operator== for wxArrayInt ?
    // Fix for profile loading issues
#if wxCHECK_VERSION(3, 1, 1)
    if (sel_printers != sel_printers_prev)
    {
#else
    auto are_equal = [](const wxArrayInt &arr_first, const wxArrayInt &arr_second)
    {
        if (arr_first.GetCount() != arr_second.GetCount())
            return false;
        for (size_t i = 0; i < arr_first.GetCount(); i++)
            if (arr_first[i] != arr_second[i])
                return false;
        return true;
    };
    if (!are_equal(sel_printers, sel_printers_prev))
    {
#endif
        // col1 getter: in vendor_first mode, col1 shows vendors; otherwise types
        auto col1_get = [this](const Preset *p) -> const std::string &
        {
            return vendor_first ? this->materials->get_vendor(p) : this->materials->get_type(p);
        };

        // Refresh col1 list (list_type widget)
        list_type->Clear();
        list_type->append(_L("(All)"), &EMPTY);
        std::vector<std::string> appended_col1;
        if (sel_printers_count > 1)
        {
            if (sel_printers[0] == 0 && sel_printers_count > 1)
            {
                if (last_selected_printer == 0)
                {
                    list_printer->SetSelection(wxNOT_FOUND);
                    list_printer->SetSelection(0);
                }
                else
                {
                    list_printer->SetSelection(0, false);
                    sel_printers_count = list_printer->GetSelections(sel_printers);
                }
            }
        }

        auto populate_col1 = [&](const Preset *printer, const std::string &printer_name)
        {
            materials->filter_presets(printer, printer_name, EMPTY, EMPTY,
                                      [this, &appended_col1, &col1_get](const Preset *p)
                                      {
                                          const std::string &val = col1_get(p);
                                          if (std::find(appended_col1.begin(), appended_col1.end(), val) ==
                                              appended_col1.end())
                                          {
                                              list_type->append(val, &val);
                                              appended_col1.emplace_back(val);
                                          }
                                      });
        };

        if (sel_printers_count > 0 && sel_printers[0] != 0)
        {
            for (int i = 0; i < sel_printers_count; i++)
            {
                const std::string &printer_name = list_printer->get_data(sel_printers[i]);
                const Preset *printer = nullptr;
                for (const Preset *it : materials->printers)
                    if (it->name == printer_name)
                    {
                        printer = it;
                        break;
                    }
                populate_col1(printer, printer_name);
            }
        }
        else if (sel_printers_count > 0 && last_selected_printer == 0)
        {
            list_printer->SetSelection(wxNOT_FOUND);
            list_printer->SetSelection(0);
            sel_printers_count = list_printer->GetSelections(sel_printers);
            populate_col1(nullptr, EMPTY);
        }
        sort_list_data(list_type, true, false);

        sel_printers_prev = sel_printers;
        sel_type = 0;
        sel_type_prev = wxNOT_FOUND;
        list_type->SetSelection(sel_type);
        list_profile->Clear();
    }

    if (sel_type != sel_type_prev)
    {
        // Refresh col2 list (list_vendor widget) based on col1 selection
        // col2 getter: opposite of col1
        auto col2_get = [this](const Preset *p) -> const std::string &
        {
            return vendor_first ? this->materials->get_type(p) : this->materials->get_vendor(p);
        };

        list_vendor->Clear();
        list_vendor->append(_L("(All)"), &EMPTY);
        std::vector<std::string> appended_col2;
        if (sel_printers_count != 0 && sel_type != wxNOT_FOUND)
        {
            const std::string &col1_val = list_type->get_data(sel_type);
            // Map col1 value to type/vendor filter params for filter_presets
            const std::string &type_filter = vendor_first ? EMPTY : col1_val;
            const std::string &vendor_filter = vendor_first ? col1_val : EMPTY;

            for (int i = 0; i < sel_printers_count; i++)
            {
                const std::string &printer_name = list_printer->get_data(sel_printers[i]);
                const Preset *printer = nullptr;
                for (const Preset *it : materials->printers)
                    if (it->name == printer_name)
                    {
                        printer = it;
                        break;
                    }
                materials->filter_presets(printer, printer_name, type_filter, vendor_filter,
                                          [this, &appended_col2, &col2_get](const Preset *p)
                                          {
                                              const std::string &val = col2_get(p);
                                              if (std::find(appended_col2.begin(), appended_col2.end(), val) ==
                                                  appended_col2.end())
                                              {
                                                  list_vendor->append(val, &val);
                                                  appended_col2.emplace_back(val);
                                              }
                                          });
            }
            sort_list_data(list_vendor, true, false);
        }

        sel_type_prev = sel_type;
        sel_vendor = 0;
        sel_vendor_prev = wxNOT_FOUND;
        list_vendor->SetSelection(sel_vendor);
        list_profile->Clear();
    }

    if (sel_vendor != sel_vendor_prev)
    {
        // Refresh material list

        list_profile->Clear();
        std::vector<std::string> appended_aliases;
        clear_compatible_printers_label();
        if (sel_printers_count != 0 && sel_type != wxNOT_FOUND && sel_vendor != wxNOT_FOUND)
        {
            // Map col1/col2 values to type/vendor filter params
            const std::string &col1_val = list_type->get_data(sel_type);
            const std::string &col2_val = list_vendor->get_data(sel_vendor);
            const std::string &type = vendor_first ? col2_val : col1_val;
            const std::string &vendor = vendor_first ? col1_val : col2_val;

            std::vector<ProfilePrintData> to_list;
            for (int i = 0; i < sel_printers_count; i++)
            {
                const std::string &printer_name = list_printer->get_data(sel_printers[i]);
                const Preset *printer = nullptr;
                for (const Preset *it : materials->printers)
                {
                    if (it->name == printer_name)
                    {
                        printer = it;
                        break;
                    }
                }
                materials->filter_presets(
                    printer, printer_name, type, vendor,
                    [this, &to_list, &appended_aliases](const Preset *p)
                    {
                        const std::string &section = materials->appconfig_section();
                        bool checked = wizard_p()->appconfig_new.has(section, p->name);
                        bool was_checked = false;

                        auto it = std::find(appended_aliases.begin(), appended_aliases.end(), p->alias);
                        size_t cur_i = 0;
                        if (it == appended_aliases.end())
                        {
                            cur_i = list_profile->append(p->alias + (materials->get_omnipresent(p) ? "" : " *"),
                                                         &p->alias);
                            appended_aliases.emplace_back(p->alias);
                            to_list.emplace_back(p->alias, materials->get_omnipresent(p), checked);
                        }
                        else
                        {
                            cur_i = it - appended_aliases.begin();
                            was_checked = list_profile->IsChecked(cur_i);
                            to_list[cur_i].checked = checked || was_checked;
                        }
                        list_profile->Check(cur_i, checked || was_checked);

                        /* Update preset selection in config.
					 * If one preset from aliases bundle is selected,
					 * than mark all presets with this aliases as selected
					 * */
                        if (checked && !was_checked)
                            wizard_p()->update_presets_in_config(section, p->alias, true);
                        else if (!checked && was_checked)
                            wizard_p()->appconfig_new.set(section, p->name, "1");
                    });
            }
            sort_list_data(list_profile, to_list);
        }

        sel_vendor_prev = sel_vendor;
    }
    wxGetApp().UpdateDarkUI(list_profile);
}

void PageMaterials::sort_list_data(StringList *list, bool add_All_item, bool material_type_ordering)
{
    // get data from list
    // sort data
    // first should be <all>
    // then preferred profiles
    // then the rest
    // in alphabetical order

    std::vector<std::reference_wrapper<const std::string>> preferred_profiles;
    std::vector<std::pair<std::wstring, std::reference_wrapper<const std::string>>>
        other_profiles; // first is lower case id for sorting
    for (int i = 0; i < list->size(); ++i)
    {
        const std::string &data = list->get_data(i);
        if (data == EMPTY) // do not sort <all> item
            continue;
        if (!material_type_ordering && data.find("preFlight") != std::string::npos)
            preferred_profiles.push_back(data);
        else
            other_profiles.emplace_back(boost::algorithm::to_lower_copy(boost::nowide::widen(data)), data);
    }
    if (material_type_ordering)
    {
        const ConfigOptionDef *def = print_config_def.get("filament_type");
        size_t end_of_sorted = 0;
        for (const std::string &value : def->enum_def->values())
        {
            for (size_t profs = end_of_sorted; profs < other_profiles.size(); profs++)
            {
                // find instead compare because PET vs PETG
                if (other_profiles[profs].second.get().find(value) != std::string::npos)
                {
                    //swap
                    if (profs != end_of_sorted)
                    {
                        std::pair<std::wstring, std::reference_wrapper<const std::string>> aux =
                            other_profiles[end_of_sorted];
                        other_profiles[end_of_sorted] = other_profiles[profs];
                        other_profiles[profs] = aux;
                    }
                    end_of_sorted++;
                    break;
                }
            }
        }
    }
    else
    {
        std::sort(preferred_profiles.begin(), preferred_profiles.end(),
                  [](std::reference_wrapper<const std::string> a, std::reference_wrapper<const std::string> b)
                  { return a.get() < b.get(); });
        std::sort(other_profiles.begin(), other_profiles.end(),
                  [](const std::pair<std::wstring, std::reference_wrapper<const std::string>> &a,
                     const std::pair<std::wstring, std::reference_wrapper<const std::string>> &b)
                  { return a.first < b.first; });
    }

    list->Clear();
    if (add_All_item)
        list->append(_L("(All)"), &EMPTY);
    for (const auto &item : preferred_profiles)
        list->append(item, &const_cast<std::string &>(item.get()));
    for (const auto &item : other_profiles)
        list->append(item.second, &const_cast<std::string &>(item.second.get()));
}

void PageMaterials::sort_list_data(PresetList *list, const std::vector<ProfilePrintData> &data)
{
    // sort data
    // then preferred profiles
    // then the rest
    // in alphabetical order
    std::vector<ProfilePrintData> preferred_profiles;
    std::vector<std::pair<std::wstring, ProfilePrintData>> other_profiles; // first is lower case id for sorting
    for (const auto &item : data)
    {
        const std::string &name = item.name;
        if (name.find("preFlight") != std::string::npos)
            preferred_profiles.emplace_back(item);
        else
            other_profiles.emplace_back(boost::algorithm::to_lower_copy(boost::nowide::widen(name)), item);
    }
    std::sort(preferred_profiles.begin(), preferred_profiles.end(),
              [](ProfilePrintData a, ProfilePrintData b) { return a.name.get() < b.name.get(); });
    std::sort(other_profiles.begin(), other_profiles.end(),
              [](const std::pair<std::wstring, ProfilePrintData> &a, const std::pair<std::wstring, ProfilePrintData> &b)
              { return a.first < b.first; });
    list->Clear();
    for (size_t i = 0; i < preferred_profiles.size(); ++i)
    {
        list->append(std::string(preferred_profiles[i].name) + (preferred_profiles[i].omnipresent ? "" : " *"),
                     &const_cast<std::string &>(preferred_profiles[i].name.get()));
        list->Check(i, preferred_profiles[i].checked);
    }
    for (size_t i = 0; i < other_profiles.size(); ++i)
    {
        list->append(std::string(other_profiles[i].second.name) + (other_profiles[i].second.omnipresent ? "" : " *"),
                     &const_cast<std::string &>(other_profiles[i].second.name.get()));
        list->Check(i + preferred_profiles.size(), other_profiles[i].second.checked);
    }
}

void PageMaterials::select_material(int i)
{
    const std::string &name = list_profile->get_data(i);
    bool checked = list_profile->IsChecked(i);
    const std::string &section = materials->appconfig_section();

    // Persist the check state to appconfig_new so it survives filter changes
    wizard_p()->update_presets_in_config(section, name, checked);
}

void PageMaterials::select_all(bool select)
{
    wxWindowUpdateLocker freeze_guard(this);
    (void) freeze_guard;

    for (unsigned i = 0; i < list_profile->GetCount(); i++)
    {
        const bool current = list_profile->IsChecked(i);
        if (current != select)
        {
            list_profile->Check(i, select);
            select_material(i);
        }
    }
}

void PageMaterials::clear()
{
    list_printer->Clear();
    list_type->Clear();
    list_vendor->Clear();
    list_profile->Clear();
    sel_printers_prev.Clear();
    sel_type_prev = wxNOT_FOUND;
    sel_vendor_prev = wxNOT_FOUND;
    presets_loaded = false;
}

void PageMaterials::on_activate()
{
    check_and_update_presets(true);
    first_paint = true;
}

const char *PageCustom::default_profile_name = "My Settings";

PageCustom::PageCustom(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Custom Printer Setup"), _L("Custom Printer"))
{
    cb_custom = new ::CheckBox(this, _L("Define a custom printer profile"));
    auto *label = new wxStaticText(this, wxID_ANY, _L("Custom profile name:"));

    wxBoxSizer *profile_name_sizer = new wxBoxSizer(wxVERTICAL);
    profile_name_editor = new SavePresetDialog::Item{this, profile_name_sizer, default_profile_name,
                                                     wxGetApp().preset_bundle};
    profile_name_editor->Enable(false);

    cb_custom->Bind(wxEVT_CHECKBOX,
                    [this](wxCommandEvent &)
                    {
                        profile_name_editor->Enable(custom_wanted());
                        wizard_p()->on_custom_setup(custom_wanted());
                    });

    append(cb_custom);
    append(label);
    append(profile_name_sizer);
}

PageUpdate::PageUpdate(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Automatic updates"), _L("Updates")), version_check(true)
{
    const AppConfig *app_config = wxGetApp().app_config;

    auto *box_version_check = new ::CheckBox(this, _L("Check for application updates"));
    box_version_check->SetValue(app_config->get("notify_release") != "none");
    append(box_version_check);
    append_text(wxString::Format(
        _L("If enabled, %s checks for new application versions online. When a new version becomes available, "
           "a notification is displayed at the next application startup (never during program usage). "
           "This is only a notification mechanisms, no automatic installation is done."),
        PREFLIGHT_APP_NAME));

    box_version_check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) { this->version_check = event.IsChecked(); });
}

namespace DownloaderUtils
{
namespace
{
#ifdef _WIN32
wxString get_downloads_path()
{
    wxString ret;
    PWSTR path = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path);
    if (SUCCEEDED(hr))
    {
        ret = wxString(path);
    }
    CoTaskMemFree(path);
    return ret;
}
#elif __APPLE__
wxString get_downloads_path()
{
    // call objective-c implementation
    return wxString::FromUTF8(get_downloads_path_mac());
}
#else
wxString get_downloads_path()
{
    wxString command = "xdg-user-dir DOWNLOAD";
    wxArrayString output;
    DSKY::desktop_execute_get_result(command, output);
    if (output.GetCount() > 0)
    {
        return output[0];
    }
    return wxString();
}
#endif
} // namespace
Worker::Worker(wxWindow *parent) : wxBoxSizer(wxHORIZONTAL), m_parent(parent)
{
    m_input_path = new wxTextCtrl(m_parent, wxID_ANY);
    set_path_name(get_app_config()->get("url_downloader_dest"));

    auto *path_label = new wxStaticText(m_parent, wxID_ANY, _L("Download path") + ":");

    int em = wxGetApp().em_unit();
    this->Add(path_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);
    this->Add(m_input_path, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, em / 2);

    auto *button_path = new wxButton(m_parent, wxID_ANY, _L("Browse"));
    wxGetApp().SetWindowVariantForButton(button_path);
    this->Add(button_path, 0, wxEXPAND | wxTOP | wxLEFT, em / 2);
    button_path->Bind(wxEVT_BUTTON,
                      [this](wxCommandEvent &event)
                      {
                          boost::filesystem::path chosen_dest(into_u8(m_input_path->GetValue()));

                          wxDirDialog dialog(m_parent, _L("Choose folder") + ":", chosen_dest.string());
                          if (dialog.ShowModal() == wxID_OK)
                              this->m_input_path->SetValue(dialog.GetPath());
                      });

    for (wxSizerItem *item : this->GetChildren())
        if (item->IsWindow())
        {
            wxWindow *win = item->GetWindow();
            wxGetApp().UpdateDarkUI(win);
        }
}

void Worker::set_path_name(wxString path)
{
    if (path.empty())
        path = boost::nowide::widen(get_app_config()->get("url_downloader_dest"));

    if (path.empty())
    {
        // What should be default path? Each system has Downloads folder, that could be good one.
        // Other would be program location folder - not so good: access rights, apple bin is inside bundle...
        // default_path = boost::dll::program_location().parent_path().string();
        path = get_downloads_path();
    }

    m_input_path->SetValue(path);
}

void Worker::set_path_name(const std::string &name)
{
    if (!m_input_path)
        return;

    set_path_name(boost::nowide::widen(name));
}

} // namespace DownloaderUtils

PageDownloader::PageDownloader(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Downloads from URL"), _L("Downloads"))
{
    const AppConfig *app_config = wxGetApp().app_config;
    auto boldfont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    boldfont.SetWeight(wxFONTWEIGHT_BOLD);

    append_spacer(GetScaledVerticalSpacing());

    auto *box_allow_downloads = new ::CheckBox(this, _L("Allow built-in downloader"));
    // Ticked by default until the app config has stored a choice.
    bool box_allow_value = (app_config->has("downloader_url_registered")
                                ? app_config->get_bool("downloader_url_registered")
                                : true);
    box_allow_downloads->SetValue(box_allow_value);
    append(box_allow_downloads);

    // append info line with link on printables.com
    {
        const int em = parent->em_unit();
        wxHtmlWindow *html_window = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxSize(60 * em, 5 * em),
                                                     wxHW_SCROLLBAR_NEVER);

        html_window->Bind(wxEVT_HTML_LINK_CLICKED,
                          [](wxHtmlLinkEvent &event)
                          {
                              wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
                              event.Skip(false);
                          });

        append(html_window);

        const auto text_clr = wxGetApp().get_label_clr_default();
        const auto bgr_clr_str = wxGetApp().get_html_bg_color(parent);
        const auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));

        const wxString link_web = format_wxstr("<a href = \"%1%\">%1%</a>", "Printables.com");
        // TRN ConfigWizard : "link" is a word from phrase
        // "For a list of supported websites, follow this link"
        const wxString link_help = format_wxstr(
            "<a href = \"%1%\">%2%</a>",
            "https://preflight3d.com/articles/article/opening-models-from-supported-websites_399198", _L("link"));

        // TRN ConfigWizard : Downloader : %1% = "printables.com", "%2%" = "link"
        const wxString main_text = format_wxstr(
            _L("Enable this option to open models from supported websites (e.g. %1%) with a single click. For a list of supported websites, follow this %2%."),
            link_web, link_help);

        const wxFont &font = this->GetFont();
        const int fs = font.GetPointSize();
        int size[] = {fs, fs, fs, fs, fs, fs, fs};
        html_window->SetFonts(font.GetFaceName(), font.GetFaceName(), size);

        html_window->SetPage(format_wxstr("<html><body bgcolor=%1% link=%2%>"
                                          "<font color=%2% size=\"3\">%3%</font>"
                                          "</body></html>",
                                          bgr_clr_str, text_clr_str, main_text));
    }

#if defined(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
    append_text(wxString::Format(_L(
        "On Linux systems the process of registration also creates desktop integration files for this version of application.")));
#endif //(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)

    box_allow_downloads->Bind(wxEVT_CHECKBOX,
                              [this](wxCommandEvent &event) { this->m_downloader->allow(event.IsChecked()); });

    m_downloader = new DownloaderUtils::Worker(this);
    append(m_downloader);
    m_downloader->allow(box_allow_value);
}

bool PageDownloader::on_finish_downloader() const
{
    return m_downloader->on_finish();
}

#ifdef __linux__
bool DownloaderUtils::Worker::perform_registration_linux = false;
#endif // __linux__

bool DownloaderUtils::Worker::perform_download_register(const std::string &path)
{
    boost::filesystem::path aux_dest(path);
    boost::system::error_code ec;
    boost::filesystem::path chosen_dest = boost::filesystem::absolute(aux_dest, ec);
    if (ec)
        chosen_dest = aux_dest;
    ec.clear();
    if (chosen_dest.empty() || !boost::filesystem::is_directory(chosen_dest, ec) || ec)
    {
        std::string err_msg = DSKY::format("%1%\n\n%2%", _L("Chosen directory for downloads does not exist."),
                                           chosen_dest.string());
        BOOST_LOG_TRIVIAL(error) << err_msg;
        show_error(/*m_parent*/ nullptr, err_msg);
        return false;
    }
    BOOST_LOG_TRIVIAL(info) << "Downloader registration: Directory for downloads: " << chosen_dest.string();
    wxGetApp().app_config->set("url_downloader_dest", chosen_dest.string());
    return perform_url_register();
}
bool DownloaderUtils::Worker::perform_url_register()
{
#ifdef _WIN32
    // Registry key creation for "preflight://" URL

    boost::filesystem::path binary_path(boost::filesystem::canonical(boost::dll::program_location()));
    // the path to binary needs to be correctly saved in string with respect to localized characters
    wxString wbinary = wxString::FromUTF8(binary_path.string());
    std::string binary_string = (boost::format("%1%") % wbinary).str();
    BOOST_LOG_TRIVIAL(info) << "Downloader registration: Path of binary: " << binary_string;

    //std::string key_string = "\"" + binary_string + "\" \"-u\" \"%1\"";
    //std::string key_string = "\"" + binary_string + "\" \"%1\"";
    std::string key_string = "\"" + binary_string + "\" \"--single-instance\" \"%1\"";

    wxRegKey key_first(wxRegKey::HKCU, "Software\\Classes\\preflight");
    wxRegKey key_full(wxRegKey::HKCU, "Software\\Classes\\preflight\\shell\\open\\command");
    if (!key_first.Exists())
    {
        key_first.Create(false);
    }
    key_first.SetValue("URL Protocol", "");

    if (!key_full.Exists())
    {
        key_full.Create(false);
    }
    // Legacy path removed
    key_full = key_string;
#elif __APPLE__
    // Apple registers for custom url in info.plist thus it has to be already registered since build.
    // The url will always trigger opening of preFlight and we have to check that user has allowed it. (GUI_App::MacOpenURL is the triggered method)
#elif defined(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
    // the performation should be called later during desktop integration
    perform_registration_linux = true;
#endif
    return true;
}

void DownloaderUtils::Worker::deregister()
{
#ifdef _WIN32
    std::string key_string = "";
    wxRegKey key_full(wxRegKey::HKCU, "Software\\Classes\\preflight\\shell\\open\\command");
    if (!key_full.Exists())
    {
        return;
    }
    key_full = key_string;
#elif __APPLE__
    // Nothing to do on macOS.
#elif defined(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
    BOOST_LOG_TRIVIAL(debug) << "DesktopIntegrationDialog::undo_downloader_registration";
    DesktopIntegrationDialog::undo_downloader_registration();
    perform_registration_linux = false;
#endif
}

bool DownloaderUtils::Worker::on_finish()
{
    AppConfig *app_config = wxGetApp().app_config;
    bool ac_value = app_config->get_bool("downloader_url_registered");
    BOOST_LOG_TRIVIAL(debug) << "PageDownloader::on_finish_downloader ac_value " << ac_value << " downloader_checked "
                             << downloader_checked;
    if (ac_value && downloader_checked)
    {
        // already registered but we need to do it again
        if (!perform_download_register(DSKY::into_u8(path_name())))
            return false;
        app_config->set("downloader_url_registered", "1");
    }
    else if (!ac_value && downloader_checked)
    {
        // register
        if (!perform_download_register(DSKY::into_u8(path_name())))
            return false;
        app_config->set("downloader_url_registered", "1");
    }
    else if (ac_value && !downloader_checked)
    {
        // deregister, downloads are banned now
        deregister();
        app_config->set("downloader_url_registered", "0");
    } /*else if (!ac_value && !downloader_checked) {
        // not registered and we dont want to do it
        // do not deregister as other instance might be registered
    } */
    return true;
}

PageReloadFromDisk::PageReloadFromDisk(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Reload from disk"), _L("Reload from disk")), full_pathnames(false)
{
    auto *box_pathnames =
        new ::CheckBox(this, _L("Export full pathnames of models and parts sources into 3mf and amf files"));
    box_pathnames->SetValue(wxGetApp().app_config->get_bool("export_sources_full_pathnames"));
    append(box_pathnames);
    append_text(
        _L("If enabled, allows the Reload from disk command to automatically find and load the files when invoked.\n"
           "If not enabled, the Reload from disk command will ask to select each file using an open file dialog."));

    box_pathnames->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) { this->full_pathnames = event.IsChecked(); });
}

#ifdef _WIN32
PageFilesAssociation::PageFilesAssociation(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Files association"), _L("Files association"))
{
    cb_3mf = new ::CheckBox(this, _L("Associate .3mf files to preFlight"));
    cb_stl = new ::CheckBox(this, _L("Associate .stl files to preFlight"));
    //    cb_gcode = new ::CheckBox(this, _L("Associate .gcode files to preFlight G-code Viewer"));

    append(cb_3mf);
    append(cb_stl);
    //    append(cb_gcode);
}
#endif // _WIN32

PageMode::PageMode(ConfigWizard *parent) : ConfigWizardPage(parent, _L("View mode"), _L("View mode"))
{
    append_text(_L("preFlight's user interfaces comes in three variants:\nSimple, Advanced, and Expert.\n"
                   "The Simple mode shows only the most frequently used settings relevant for regular 3D printing. "
                   "The other two offer progressively more sophisticated fine-tuning, "
                   "they are suitable for advanced and expert users, respectively."));

    radio_simple = new wxRadioButton(this, wxID_ANY, _L("Simple mode"));
    radio_advanced = new wxRadioButton(this, wxID_ANY, _L("Advanced mode"));
    radio_expert = new wxRadioButton(this, wxID_ANY, _L("Expert mode"));

    std::string mode{"simple"};
    wxGetApp().app_config->get("", "view_mode", mode);

    if (mode == "advanced")
    {
        radio_advanced->SetValue(true);
    }
    else if (mode == "expert")
    {
        radio_expert->SetValue(true);
    }
    else
    {
        radio_simple->SetValue(true);
    }

    append(radio_simple);
    append(radio_advanced);
    append(radio_expert);

    append_text("\n" + _L("The size of the object can be specified in inches"));
    check_inch = new ::CheckBox(this, _L("Use inches"));
    check_inch->SetValue(wxGetApp().app_config->get_bool("use_inches"));
    append(check_inch);

    on_activate();
}

void PageMode::serialize_mode(AppConfig *app_config) const
{
    std::string mode = "";

    if (radio_simple->GetValue())
    {
        mode = "simple";
    }
    if (radio_advanced->GetValue())
    {
        mode = "advanced";
    }
    if (radio_expert->GetValue())
    {
        mode = "expert";
    }

    app_config->set("view_mode", mode);
    app_config->set("use_inches", check_inch->GetValue() ? "1" : "0");
}

wxString repo_title(const std::string &repo_id, const std::string &repo_name)
{
    if (repo_name.empty())
    {
        return repo_id.empty() ? wxString::FromUTF8("Unknown repo") : format_wxstr("Unnamed repo (ID %1%)", repo_id);
    }
    return repo_name;
}

PageVendors::PageVendors(ConfigWizard *parent, std::string repo_id /*= wxEmptyString*/, std::string repo_name)
    : ConfigWizardPage(parent, repo_title(repo_id, repo_name), repo_title(repo_id, repo_name))
{
    const AppConfig &appconfig = this->wizard_p()->appconfig_new;

    append_text(wxString::Format(_L("Pick another vendor supported by %s"), PREFLIGHT_APP_NAME) + ":");

    auto boldfont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    boldfont.SetWeight(wxFONTWEIGHT_BOLD);
    // Copy vendors from bundle map to vector, so we can sort it without case sensitivity
    std::vector<std::pair<std::wstring, const VendorProfile *>> vendors;
    for (const auto &pair : wizard_p()->bundles)
    {
        if (pair.second.vendor_profile->repo_id != repo_id)
            continue;

        vendors.emplace_back(boost::algorithm::to_lower_copy(boost::nowide::widen(pair.second.vendor_profile->name)),
                             pair.second.vendor_profile);
    }

    std::sort(vendors.begin(), vendors.end(),
              [](const std::pair<std::wstring, const VendorProfile *> &a,
                 const std::pair<std::wstring, const VendorProfile *> &b) { return a.first < b.first; });

    for (const std::pair<std::wstring, const VendorProfile *> &v : vendors)
    {
        const VendorProfile *vendor = v.second;
        auto *cbox = new ::CheckBox(this, vendor->name);
        cbox->Bind(
            wxEVT_CHECKBOX,
            [=](wxCommandEvent &event)
            {
                if (cbox->GetValue())
                {
                    // create PrinterPages for this vendor, if they aren't created jet
                    {
                        auto repo = wizard_p()->get_repo(repo_id);
                        assert(repo);
                        if (repo->printers_pages.find(vendor->id) == repo->printers_pages.end())
                        {
                            wxWindowUpdateLocker freeze_guard(parent);
                            wizard_p()->create_vendor_printers_page(repo_id, vendor);
                        }
                    }

                    wxString user_presets_list{wxString()};
                    int user_presets_cnt{0};

                    // Check if some of preset doesn't exist as a user_preset
                    // to avoid rewrite those user_presets by new installed system presets
                    const PresetCollection &presets = wizard_p()->bundles.at(vendor->id).preset_bundle.get()->printers;
                    for (const Preset &preset : presets)
                        if (!preset.is_default && boost::filesystem::exists(preset.file))
                        {
                            user_presets_list += " * " + from_u8(preset.name) + "\n";
                            user_presets_cnt++;
                        }

                    if (!user_presets_list.IsEmpty())
                    {
                        wxString message = format_wxstr(
                            _L_PLURAL(
                                "Existing user preset '%2%' has the same name as one of new system presets from vendor '%1%'.\n"
                                "Please note that this user preset will be rewritten by the system preset.\n\n"
                                "Do you still wish to add presets from vendor '%1%'?",
                                "Existing user presets (%2%) have the same names as some of new system presets from vendor '%1%'.\n"
                                "Please note that these user presets will be rewritten by the system presets.\n\n"
                                "Do you still wish to add presets from vendor '%1%'?",
                                user_presets_cnt),
                            vendor->name, user_presets_list);

                        MessageDialog msg(this->GetParent(), message, _L("Notice"), wxYES_NO);
                        if (msg.ShowModal() == wxID_NO)
                        {
                            // uncheck checked ckeckbox
                            cbox->SetValue(false);
                            return;
                        }
                    }
                }

                // Vendor install/uninstall is handled by the Choose Vendors page
            });

        const auto &acvendors = appconfig.vendors();
        const bool enabled = acvendors.find(vendor->id) != acvendors.end();
        if (enabled)
        {
            cbox->SetValue(true);
            wizard_p()->create_vendor_printers_page(repo_id, vendor, true);
        }

        append(cbox);
    }
}

PageFirmware::PageFirmware(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Firmware Type"), _L("Firmware"), 1)
    , gcode_opt(*print_config_def.get("gcode_flavor"))
    , gcode_picker(nullptr)
{
    append_text(_L("Choose the type of firmware used by your printer."));
    append_text(_(gcode_opt.tooltip));

    wxArrayString choices;
    choices.Alloc(gcode_opt.enum_def->labels().size());
    for (const auto &label : gcode_opt.enum_def->labels())
    {
        choices.Add(label);
    }

    gcode_picker = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
    wxGetApp().UpdateDarkUI(gcode_picker);
    const auto &enum_values = gcode_opt.enum_def->values();
    auto needle = enum_values.cend();
    if (gcode_opt.default_value)
    {
        needle = std::find(enum_values.cbegin(), enum_values.cend(), gcode_opt.default_value->serialize());
    }
    if (needle != enum_values.cend())
    {
        gcode_picker->SetSelection(needle - enum_values.cbegin());
    }
    else
    {
        gcode_picker->SetSelection(0);
    }

    append(gcode_picker);
}

void PageFirmware::apply_custom_config(DynamicPrintConfig &config)
{
    auto sel = gcode_picker->GetSelection();
    if (sel >= 0 && (size_t) sel < gcode_opt.enum_def->labels().size())
    {
        auto *opt = new ConfigOptionEnum<GCodeFlavor>(static_cast<GCodeFlavor>(sel));
        config.set_key_value("gcode_flavor", opt);
    }
}

static void focus_event(wxFocusEvent &e, wxTextCtrl *ctrl, double def_value)
{
    e.Skip();
    wxString str = ctrl->GetValue();

    const char dec_sep = is_decimal_separator_point() ? '.' : ',';
    const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
    // Replace the first incorrect separator in decimal number.
    bool was_replaced = str.Replace(dec_sep_alt, dec_sep, false) != 0;

    double val = 0.0;
    if (!str.ToDouble(&val))
    {
        if (val == 0.0)
            val = def_value;
        ctrl->SetValue(double_to_string(val));
        show_error(nullptr, _L("Invalid numeric input."));
        // On Windows, this SetFocus creates an invisible marker.
        //ctrl->SetFocus();
    }
    else if (was_replaced)
        ctrl->SetValue(double_to_string(val));
}

class DiamTextCtrl : public wxTextCtrl
{
public:
    DiamTextCtrl(wxWindow *parent)
    {
#ifdef _WIN32
        long style = wxBORDER_SIMPLE;
#else
        long style = 0;
#endif
        Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
               wxSize(Field::def_width_thinner() * wxGetApp().em_unit(), wxDefaultCoord), style);
        wxGetApp().UpdateDarkUI(this);
    }
    ~DiamTextCtrl() {}
};

PageBedShape::PageBedShape(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Bed Shape and Size"), _L("Bed Shape"), 1), shape_panel(new BedShapePanel(this))
{
    append_text(_L("Set the shape of your printer's bed."));

    shape_panel->build_panel(*wizard_p()->custom_config->option<ConfigOptionPoints>("bed_shape"),
                             *wizard_p()->custom_config->option<ConfigOptionString>("bed_custom_texture"),
                             *wizard_p()->custom_config->option<ConfigOptionString>("bed_custom_model"));

    append(shape_panel);
}

void PageBedShape::apply_custom_config(DynamicPrintConfig &config)
{
    const std::vector<Vec2d> &points = shape_panel->get_shape();
    const std::string &custom_texture = shape_panel->get_custom_texture();
    const std::string &custom_model = shape_panel->get_custom_model();
    config.set_key_value("bed_shape", new ConfigOptionPoints(points));
    config.set_key_value("bed_custom_texture", new ConfigOptionString(custom_texture));
    config.set_key_value("bed_custom_model", new ConfigOptionString(custom_model));
}

PageBuildVolume::PageBuildVolume(ConfigWizard *parent)
    // TRN ConfigWizard : Size of possible print, related on printer size
    : ConfigWizardPage(parent, _L("Build Volume"), _L("Build Volume"), 1), build_volume(new DiamTextCtrl(this))
{
    append_text(_L("Set the printer height."));

    wxString value = "200";
    build_volume->SetValue(value);

    build_volume->Bind(
        wxEVT_KILL_FOCUS,
        [this](wxFocusEvent &e)
        {
            double def_value = 200.0;
            double max_value = 1200.0;
            e.Skip();
            wxString str = build_volume->GetValue();

            const char dec_sep = is_decimal_separator_point() ? '.' : ',';
            const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
            // Replace the first incorrect separator in decimal number.
            bool was_replaced = str.Replace(dec_sep_alt, dec_sep, false) != 0;

            double val = 0.0;
            if (!str.ToDouble(&val))
            {
                val = def_value;
                build_volume->SetValue(double_to_string(val));
                show_error(nullptr, _L("Invalid numeric input."));
                //build_volume->SetFocus();
            }
            else if (val < 0.0)
            {
                val = def_value;
                build_volume->SetValue(double_to_string(val));
                show_error(nullptr, _L("Invalid numeric input."));
                //build_volume->SetFocus();
            }
            else if (val > max_value)
            {
                val = max_value;
                build_volume->SetValue(double_to_string(val));
                show_error(nullptr, _L("Invalid numeric input."));
                //build_volume->SetFocus();
            }
            else if (was_replaced)
                build_volume->SetValue(double_to_string(val));
        },
        build_volume->GetId());

    auto *sizer_volume = new wxFlexGridSizer(3, wxGetApp().em_unit() / 2, wxGetApp().em_unit() / 2);
    auto *text_volume = new wxStaticText(this, wxID_ANY, _L("Max print height") + ":");
    auto *unit_volume = new wxStaticText(this, wxID_ANY, _L("mm"));
    sizer_volume->AddGrowableCol(0, 1);
    sizer_volume->Add(text_volume, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_volume->Add(build_volume);
    sizer_volume->Add(unit_volume, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_volume);
}

void PageBuildVolume::apply_custom_config(DynamicPrintConfig &config)
{
    double val = 0.0;
    build_volume->GetValue().ToDouble(&val);
    auto *opt_volume = new ConfigOptionFloat(val);
    config.set_key_value("max_print_height", opt_volume);
}

PageDiameters::PageDiameters(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Filament and Nozzle Diameters"), _L("Print Diameters"), 1)
    , diam_nozzle(new DiamTextCtrl(this))
    , diam_filam(new DiamTextCtrl(this))
{
    auto *default_nozzle = print_config_def.get("nozzle_diameter")->get_default_value<ConfigOptionFloats>();
    wxString value = double_to_string(
        default_nozzle != nullptr && default_nozzle->size() > 0 ? default_nozzle->get_at(0) : 0.5);
    diam_nozzle->SetValue(value);

    auto *default_filam = print_config_def.get("filament_diameter")->get_default_value<ConfigOptionFloats>();
    value = double_to_string(default_filam != nullptr && default_filam->size() > 0 ? default_filam->get_at(0) : 3.0);
    diam_filam->SetValue(value);

    diam_nozzle->Bind(
        wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) { focus_event(e, diam_nozzle, 0.5); }, diam_nozzle->GetId());
    diam_filam->Bind(
        wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) { focus_event(e, diam_filam, 3.0); }, diam_filam->GetId());

    append_text(_L("Enter the diameter of your printer's hot end nozzle."));

    auto *sizer_nozzle = new wxFlexGridSizer(3, wxGetApp().em_unit() / 2, wxGetApp().em_unit() / 2);
    auto *text_nozzle = new wxStaticText(this, wxID_ANY, _L("Nozzle Diameter") + ":");
    auto *unit_nozzle = new wxStaticText(this, wxID_ANY, _L("mm"));
    sizer_nozzle->AddGrowableCol(0, 1);
    sizer_nozzle->Add(text_nozzle, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_nozzle->Add(diam_nozzle);
    sizer_nozzle->Add(unit_nozzle, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_nozzle);

    append_spacer(GetScaledVerticalSpacing());

    append_text(_L("Enter the diameter of your filament."));
    append_text(_L(
        "Good precision is required, so use a caliper and do multiple measurements along the filament, then compute the average."));

    auto *sizer_filam = new wxFlexGridSizer(3, wxGetApp().em_unit() / 2, wxGetApp().em_unit() / 2);
    auto *text_filam = new wxStaticText(this, wxID_ANY, _L("Filament Diameter") + ":");
    auto *unit_filam = new wxStaticText(this, wxID_ANY, _L("mm"));
    sizer_filam->AddGrowableCol(0, 1);
    sizer_filam->Add(text_filam, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_filam->Add(diam_filam, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_filam->Add(unit_filam, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_filam);
}

void PageDiameters::apply_custom_config(DynamicPrintConfig &config)
{
    double val = 0.0;
    diam_nozzle->GetValue().ToDouble(&val);
    auto *opt_nozzle = new ConfigOptionFloats(1, val);
    config.set_key_value("nozzle_diameter", opt_nozzle);

    val = 0.0;
    diam_filam->GetValue().ToDouble(&val);
    auto *opt_filam = new ConfigOptionFloats(1, val);
    config.set_key_value("filament_diameter", opt_filam);

    auto set_extrusion_width = [&config, opt_nozzle](const char *key, double dmr)
    {
        char buf[64]; // locales don't matter here (sprintf/atof)
        sprintf(buf, "%.2lf", dmr * opt_nozzle->values.front() / 0.4);
        config.set_key_value(key, new ConfigOptionFloatOrPercent(atof(buf), false));
    };

    set_extrusion_width("support_material_extrusion_width", 0.35);
    set_extrusion_width("top_infill_extrusion_width", 0.40);
    set_extrusion_width("first_layer_extrusion_width", 0.42);

    set_extrusion_width("extrusion_width", 0.45);
    set_extrusion_width("perimeter_extrusion_width", 0.45);
    set_extrusion_width("external_perimeter_extrusion_width", 0.45);
    set_extrusion_width("infill_extrusion_width", 0.45);
    set_extrusion_width("solid_infill_extrusion_width", 0.45);
}

class SpinCtrlDouble : public ::SpinInputDouble
{
public:
    SpinCtrlDouble(wxWindow *parent)
    {
#ifdef _WIN32
        long style = wxSP_ARROW_KEYS | wxBORDER_SIMPLE;
#else
        long style = wxSP_ARROW_KEYS;
#endif
        Create(parent, "", wxEmptyString, wxDefaultPosition, wxSize(6 * wxGetApp().em_unit(), -1), style);
        this->Refresh();
    }
    ~SpinCtrlDouble() {}
};

PageTemperatures::PageTemperatures(ConfigWizard *parent)
    : ConfigWizardPage(parent, _L("Nozzle and Bed Temperatures"), _L("Temperatures"), 1)
    , spin_extr(new SpinCtrlDouble(this))
    , spin_bed(new SpinCtrlDouble(this))
{
    spin_extr->SetIncrement(5.0);
    const auto &def_extr = *print_config_def.get("temperature");
    spin_extr->SetRange(def_extr.min, def_extr.max);
    auto *default_extr = def_extr.get_default_value<ConfigOptionInts>();
    spin_extr->SetValue(default_extr != nullptr && default_extr->size() > 0 ? default_extr->get_at(0) : 200);

    spin_bed->SetIncrement(5.0);
    const auto &def_bed = *print_config_def.get("bed_temperature");
    spin_bed->SetRange(def_bed.min, def_bed.max);
    auto *default_bed = def_bed.get_default_value<ConfigOptionInts>();
    spin_bed->SetValue(default_bed != nullptr && default_bed->size() > 0 ? default_bed->get_at(0) : 0);

    append_text(_L("Enter the temperature needed for extruding your filament."));
    append_text(_L("A rule of thumb is 160 to 230 °C for PLA, and 215 to 250 °C for ABS."));

    auto *sizer_extr = new wxFlexGridSizer(3, wxGetApp().em_unit() / 2, wxGetApp().em_unit() / 2);
    auto *text_extr = new wxStaticText(this, wxID_ANY, _L("Extrusion Temperature:"));
    auto *unit_extr = new wxStaticText(this, wxID_ANY, _L("°C"));
    sizer_extr->AddGrowableCol(0, 1);
    sizer_extr->Add(text_extr, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_extr->Add(spin_extr);
    sizer_extr->Add(unit_extr, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_extr);

    append_spacer(GetScaledVerticalSpacing());

    append_text(_L("Enter the bed temperature needed for getting your filament to stick to your heated bed."));
    append_text(_L("A rule of thumb is 60 °C for PLA and 110 °C for ABS. Leave zero if you have no heated bed."));

    auto *sizer_bed = new wxFlexGridSizer(3, wxGetApp().em_unit() / 2, wxGetApp().em_unit() / 2);
    auto *text_bed = new wxStaticText(this, wxID_ANY, _L("Bed Temperature") + ":");
    auto *unit_bed = new wxStaticText(this, wxID_ANY, _L("°C"));
    sizer_bed->AddGrowableCol(0, 1);
    sizer_bed->Add(text_bed, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_bed->Add(spin_bed);
    sizer_bed->Add(unit_bed, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_bed);
}

void PageTemperatures::apply_custom_config(DynamicPrintConfig &config)
{
    auto *opt_extr = new ConfigOptionInts(1, spin_extr->GetValue());
    config.set_key_value("temperature", opt_extr);
    auto *opt_extr1st = new ConfigOptionInts(1, spin_extr->GetValue());
    config.set_key_value("first_layer_temperature", opt_extr1st);
    auto *opt_bed = new ConfigOptionInts(1, spin_bed->GetValue());
    config.set_key_value("bed_temperature", opt_bed);
    auto *opt_bed1st = new ConfigOptionInts(1, spin_bed->GetValue());
    config.set_key_value("first_layer_bed_temperature", opt_bed1st);
}

// Index

ConfigWizardIndex::ConfigWizardIndex(wxWindow *parent)
    : wxPanel(parent)
    // Logo size is calculated in constructor body to account for DPI
    , bullet_black(ScalableBitmap(parent, "bullet_black.png"))
    , bullet_blue(ScalableBitmap(parent, "bullet_blue.png"))
    , bullet_white(ScalableBitmap(parent, "bullet_white.png"))
    , item_active(NO_ITEM)
    , item_hover(NO_ITEM)
    , last_page((size_t) -1)
{
#ifndef __WXOSX__
    SetDoubleBuffered(true); // SetDoubleBuffered exists on Win and Linux/GTK, but is missing on OSX
#endif                       //__WXOSX__

    // Load logo at fixed 192 physical pixels regardless of DPI scaling
    // Divide by scale factor so the final rendered size is always 192px
    const int logo_physical_size = 192;
    double scale_factor = parent->GetDPIScaleFactor();
    int logo_dip_size = std::max(1, static_cast<int>(logo_physical_size / scale_factor));
    bg = ScalableBitmap(parent, "preFlight", logo_dip_size);

    {
        wxBitmap bmp = bg.get_bitmap();
        wxImage img = bmp.ConvertToImage();
        if (!img.HasAlpha())
            img.InitAlpha();
        unsigned char *alpha = img.GetAlpha();
        const int size = img.GetWidth() * img.GetHeight();
        for (int i = 0; i < size; ++i)
            alpha[i] = static_cast<unsigned char>(alpha[i] * 0.50);
        bg.SetBitmap(wxBitmapBundle::FromBitmap(wxBitmap(img)));
    }

    SetMinSize(bg.GetSize());

    const wxSize size = GetTextExtent("m");
    em_w = size.x;
    em_h = size.y;

    Bind(wxEVT_PAINT, &ConfigWizardIndex::on_paint, this);
    Bind(wxEVT_SIZE,
         [this](wxEvent &e)
         {
             e.Skip();
             Refresh();
         });
    Bind(wxEVT_MOTION, &ConfigWizardIndex::on_mouse_move, this);

    Bind(wxEVT_LEAVE_WINDOW,
         [this](wxMouseEvent &evt)
         {
             if (item_hover != -1)
             {
                 item_hover = -1;
                 Refresh();
             }
             evt.Skip();
         });

    Bind(wxEVT_LEFT_UP,
         [this](wxMouseEvent &evt)
         {
             if (item_hover >= 0)
             {
                 go_to(item_hover);
             }
         });
}

wxDECLARE_EVENT(EVT_INDEX_PAGE, wxCommandEvent);

void ConfigWizardIndex::add_page(ConfigWizardPage *page)
{
    last_page = items.size();
    items.emplace_back(Item{page->shortname, page->indent, page});
    Refresh();
}

void ConfigWizardIndex::add_label(wxString label, unsigned indent)
{
    items.emplace_back(Item{std::move(label), indent, nullptr});
    Refresh();
}

ConfigWizardPage *ConfigWizardIndex::active_page() const
{
    if (item_active >= items.size())
    {
        return nullptr;
    }

    return items[item_active].page;
}

void ConfigWizardIndex::go_prev()
{
    // Search for a preceiding item that is a page (not a label, ie. page != nullptr)

    if (item_active == NO_ITEM)
    {
        return;
    }

    for (size_t i = item_active; i > 0; i--)
    {
        if (items[i - 1].page != nullptr)
        {
            go_to(i - 1);
            return;
        }
    }
}

void ConfigWizardIndex::go_next()
{
    // Search for a next item that is a page (not a label, ie. page != nullptr)

    if (item_active == NO_ITEM)
    {
        return;
    }

    for (size_t i = item_active + 1; i < items.size(); i++)
    {
        if (items[i].page != nullptr)
        {
            go_to(i);
            return;
        }
    }
}

// This one actually performs the go-to op
void ConfigWizardIndex::go_to(size_t i)
{
    if (i != item_active && i < items.size() && items[i].page != nullptr)
    {
        auto *former_active = active_page();
        if (former_active != nullptr)
        {
            former_active->Hide();
        }

        auto *new_active = items[i].page;
        item_active = i;
        new_active->Show();

        wxCommandEvent evt(EVT_INDEX_PAGE, GetId());
        AddPendingEvent(evt);

        Refresh();

        new_active->on_activate();
    }
}

void ConfigWizardIndex::go_to(const ConfigWizardPage *page)
{
    if (page == nullptr)
    {
        return;
    }

    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i].page == page)
        {
            go_to(i);
            return;
        }
    }
}

void ConfigWizardIndex::clear()
{
    auto *former_active = active_page();
    if (former_active != nullptr)
    {
        former_active->Hide();
    }

    items.clear();
    item_active = NO_ITEM;
}

void ConfigWizardIndex::on_paint(wxPaintEvent &evt)
{
    const auto size = GetClientSize();
    if (size.GetHeight() == 0 || size.GetWidth() == 0)
    {
        return;
    }

    wxPaintDC dc(this);

    const auto bullet_w = bullet_black.GetWidth();
    const auto bullet_h = bullet_black.GetHeight();
    const int yoff_icon = bullet_h < em_h ? (em_h - bullet_h) / 2 : 0;
    const int yoff_text = bullet_h > em_h ? (bullet_h - em_h) / 2 : 0;
    const int yinc = item_height();

    int index_width = 0;

    unsigned y = 0;
    for (size_t i = 0; i < items.size(); i++)
    {
        const Item &item = items[i];
        unsigned x = em_w / 2 + item.indent * em_w;

        if (i == item_active || (item_hover >= 0 && i == (size_t) item_hover))
        {
            dc.DrawBitmap(bullet_blue.get_bitmap(), x, y + yoff_icon, false);
        }
        else if (i < item_active)
        {
            dc.DrawBitmap(bullet_black.get_bitmap(), x, y + yoff_icon, false);
        }
        else if (i > item_active)
        {
            dc.DrawBitmap(bullet_white.get_bitmap(), x, y + yoff_icon, false);
        }

        x += +bullet_w + em_w / 2;
        const auto text_size = dc.GetTextExtent(item.label);
        dc.SetTextForeground(wxGetApp().get_label_clr_default());
        dc.DrawText(item.label, x, y + yoff_text);

        y += yinc;
        index_width = std::max(index_width, (int) x + text_size.x);
    }

    //draw logo
    if (int y = size.y - bg.GetHeight(); y >= 0)
    {
        dc.DrawBitmap(bg.get_bitmap(), 0, y, false);
        index_width = std::max(index_width, bg.GetWidth() + em_w / 2);
    }

    if (GetMinSize().x < index_width)
    {
        CallAfter(
            [this, index_width]()
            {
                SetMinSize(wxSize(index_width, GetMinSize().y));
                Refresh();
            });
    }
}

void ConfigWizardIndex::on_mouse_move(wxMouseEvent &evt)
{
    const wxClientDC dc(this);
    const wxPoint pos = evt.GetLogicalPosition(dc);

    const ssize_t item_hover_new = pos.y / item_height();

    if (item_hover_new < ssize_t(items.size()) && item_hover_new != item_hover)
    {
        item_hover = item_hover_new;
        Refresh();
    }

    evt.Skip();
}

void ConfigWizardIndex::msw_rescale()
{
    const wxSize size = GetTextExtent("m");
    em_w = size.x;
    em_h = size.y;

    SetMinSize(bg.GetSize());

    Refresh();
}

// Materials

const std::string Materials::UNKNOWN = "(Unknown)";

void Materials::push(const Preset *preset)
{
    presets.emplace_back(preset);
    types.insert(Materials::get_filament_type(preset));
}

void Materials::add_printer(const Preset *preset)
{
    printers.insert(preset);
}

void Materials::clear()
{
    presets.clear();
    types.clear();
    printers.clear();
    compatibility_counter.clear();
}

const std::string &Materials::appconfig_section() const
{
    return AppConfig::SECTION_FILAMENTS;
}

const std::string &Materials::get_type(const Preset *preset) const
{
    return get_filament_type(preset);
}

const std::string &Materials::get_vendor(const Preset *preset) const
{
    return get_filament_vendor(preset);
}

const std::string &Materials::get_filament_type(const Preset *preset)
{
    const auto *opt = preset->config.opt<ConfigOptionStrings>("filament_type");
    if (opt != nullptr && opt->values.size() > 0)
    {
        return opt->values[0];
    }
    else
    {
        return UNKNOWN;
    }
}

const std::string &Materials::get_filament_vendor(const Preset *preset)
{
    const auto *opt = preset->config.opt<ConfigOptionString>("filament_vendor");
    return opt != nullptr ? opt->value : UNKNOWN;
}

// priv

void ConfigWizard::priv::load_pages()
{
    wxWindowUpdateLocker freeze_guard(q);
    (void) freeze_guard;

    const ConfigWizardPage *former_active = index->active_page();

    index->clear();

    index->add_page(page_welcome);
    index->add_page(page_update_manager);

    if (is_config_from_archive)
    {
        // Printers
        for (const auto page : pages_fff)
            index->add_page(page);

        for (const auto &repos : repositories)
        {
            if (!repos.vendors_page)
                continue;
            index->add_page(repos.vendors_page);

            // Copy pages names from map to vector, so we can sort it without case sensitivity
            std::vector<std::pair<std::wstring, std::string>> sorted_vendors;
            for (const auto &pages : repos.printers_pages)
            {
                sorted_vendors.emplace_back(boost::algorithm::to_lower_copy(boost::nowide::widen(pages.first)),
                                            pages.first);
            }
            std::sort(sorted_vendors.begin(), sorted_vendors.end(),
                      [](const std::pair<std::wstring, std::string> &a, const std::pair<std::wstring, std::string> &b)
                      { return a.first < b.first; });

            for (const std::pair<std::wstring, std::string> &v : sorted_vendors)
            {
                const auto &pages = repos.printers_pages.find(v.second);
                if (pages == repos.printers_pages.end())
                    continue; // Should not happen
                if (PagePrinters *page = pages->second; page && page->install)
                    index->add_page(page);
            }
        }

        index->add_page(page_custom);
        if (page_custom->custom_wanted())
        {
            index->add_page(page_firmware);
            index->add_page(page_bed);
            index->add_page(page_bvolume);
            index->add_page(page_diams);
            index->add_page(page_temps);
        }

        // Filaments page always shown - filaments are independent of printer selection
        if (page_filaments)
            index->add_page(page_filaments);

        index->add_page(page_update);
        index->add_page(page_downloader);
        index->add_page(page_reload_from_disk);
#ifdef _WIN32
        index->add_page(page_files_association);
#endif // _WIN32
        // index->add_page(page_mode);
    }

    if (former_active != page_update_manager)
    {
        if (pages_fff.empty() && installed_multivendors_repos())
            index->go_to(repositories[0].vendors_page); // Activate Vendor page, if no one printer is selected
        else
            index->go_to(former_active); // Will restore the active item/page if possible
    }

    q->Layout();
    // This Refresh() is needed to avoid ugly artifacts after printer selection, when no one vendor was selected from the very beginnig
    q->Refresh();
}

void ConfigWizard::priv::init_dialog_size()
{
    // Clamp the Wizard size based on screen dimensions

    const auto idx = wxDisplay::GetFromWindow(q);
    wxDisplay display(idx != wxNOT_FOUND ? idx : 0u);

    const auto disp_rect = display.GetClientArea();
    wxRect window_rect(disp_rect.x + disp_rect.width / 20, disp_rect.y + disp_rect.height / 20,
                       9 * disp_rect.width / 10, 9 * disp_rect.height / 10);

    int min_width = em();
    for (auto page : pages_fff)
        min_width = std::max(min_width, page->get_width());

    const int width_hint = index->GetSize().GetWidth() +
                           std::max(90 * em(),
                                    min_width + 30 * em()); // padding so the widest page is not clipped
    if (width_hint < window_rect.width)
    {
        window_rect.x += (window_rect.width - width_hint) / 2;
        window_rect.width = width_hint;
    }

    q->SetSize(window_rect);
}

void ConfigWizard::priv::load_vendors()
{
    // Ask for permission to go online, then fetch and download profiles
    MessageDialog dlg(q,
                      _L("preFlight needs to download printer and filament profiles from the internet.\n\n"
                         "Would you like to connect now?"),
                      _L("Download Profiles"), wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION);
    if (dlg.ShowModal() == wxID_YES)
    {
        vendor_index = ProfileServer::fetch_index();

        if (!vendor_index.empty())
        {
            int total = static_cast<int>(vendor_index.size());
            int current = 0;
            wxProgressDialog progress(_L("Downloading Profiles"), _L("Connecting..."), total, q,
                                      wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH);
            progress.SetMinSize(wxSize(40 * wxGetApp().em_unit(), -1));
            progress.Fit();

            for (const auto &[vid, version] : vendor_index)
            {
                progress.Update(current, format_wxstr(_L("Downloading %1% (%2% of %3%)"), vid, current + 1, total));
                wxTheApp->Yield();

                if (!ProfileServer::is_vendor_cached(vid))
                    ProfileServer::fetch_vendor_ini(vid);

                ++current;
            }

            // Download unified filaments database
            progress.Update(total - 1, _L("Downloading filament database..."));
            wxTheApp->Yield();
            ProfileServer::fetch_filaments_bundle();
        }
    }

    // Load all vendor profiles from data_dir()/vendor/
    bundles = BundleMap::load();

    // Load up the set of vendors / models / variants the user has had enabled up till now
    AppConfig *app_config = wxGetApp().app_config;
    // Don't copy previous vendor selections - wizard starts fresh

    for (const auto &printer : wxGetApp().preset_bundle->printers)
    {
        if (!printer.is_default && !printer.is_system && printer.is_visible)
        {
            custom_printer_in_bundle = true;
            break;
        }
    }

    // Don't pre-select printers or filaments - wizard starts with all unchecked

    load_filaments_ini();
}

void ConfigWizard::priv::load_filaments_ini()
{
    standalone_filaments.clear();

    auto filaments_path = boost::filesystem::path(Luminary::data_dir()) / "vendor" / "Filaments.ini";
    if (!boost::filesystem::exists(filaments_path))
        return;

    namespace pt = boost::property_tree;
    pt::ptree tree;
    try
    {
        boost::nowide::ifstream ifs(filaments_path.string());
        pt::read_ini(ifs, tree);
    }
    catch (const std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << "Failed to parse Filaments.ini: " << e.what();
        return;
    }

    size_t count = 0;
    std::unique_ptr<DynamicPrintConfig> default_config(
        DynamicPrintConfig::new_from_defaults_keys(Preset::filament_options()));

    for (const auto &section : tree)
    {
        if (section.first.rfind("filament:", 0) != 0)
            continue;

        std::string name = section.first.substr(9);

        Preset preset(Preset::TYPE_FILAMENT, name, false);
        preset.is_visible = true;
        preset.config = *default_config;

        // Override with values from the .ini
        std::string vendor_name;
        std::string filament_type;

        for (const auto &kv : section.second)
        {
            const std::string &key = kv.first;
            const std::string &val = kv.second.data();

            if (key == "filament_vendor")
            {
                vendor_name = val;
                continue;
            }
            if (key == "filament_type")
            {
                filament_type = val;
                continue;
            }

            ConfigOption *opt = preset.config.optptr(key);
            if (opt)
            {
                try
                {
                    opt->deserialize(val);
                }
                catch (...)
                {
                }
            }
        }

        // Set type and vendor on the config
        if (!filament_type.empty())
            if (auto *opt = preset.config.optptr("filament_type"))
                opt->deserialize(filament_type);
        if (!vendor_name.empty())
            if (auto *opt = preset.config.optptr("filament_vendor"))
                opt->deserialize(vendor_name);

        preset.alias = name;
        standalone_filaments.push_back(std::move(preset));
        ++count;
    }

    BOOST_LOG_TRIVIAL(info) << "Loaded " << count << " filaments from Filaments.ini";
}

void ConfigWizard::priv::download_vendor_resources(const std::string &vendor_id)
{
    auto it = bundles.find(vendor_id);
    if (it == bundles.end())
        return;

    const VendorProfile *vp = it->second.vendor_profile;
    if (!vp)
        return;

    for (const auto &model : vp->models)
    {
        for (const std::string &res : {model.thumbnail, model.bed_model, model.bed_texture})
        {
            if (!res.empty())
            {
                auto res_path = ProfileServer::vendor_resource_path(vendor_id, res);
                if (!boost::filesystem::exists(res_path))
                    ProfileServer::fetch_resource(vendor_id, res);
            }
        }
    }
}

void ConfigWizard::priv::add_page(ConfigWizardPage *page)
{
    const int proportion = (page == page_filaments);
    hscroll_sizer->Add(page, proportion, wxEXPAND);
    all_pages.push_back(page);
}

void ConfigWizard::priv::enable_next(bool enable)
{
    btn_next->Enable(enable);
    btn_finish->Enable(enable);
}

void ConfigWizard::priv::set_start_page(ConfigWizard::StartPage start_page)
{
    switch (start_page)
    {
    case ConfigWizard::SP_PRINTERS:
    {
        // find start
        PagePrinters *page = pages_fff.empty() ? nullptr : pages_fff[0];
        for (const auto &repo : repositories)
        {
            if (page)
                break;
            for (const auto &[name, pages] : repo.printers_pages)
            {
                if (pages && pages->install)
                {
                    page = pages;
                    break;
                }
            }
        }

        index->go_to(page);
        btn_next->SetFocus();
    }
    break;
    case ConfigWizard::SP_FILAMENTS:
        index->go_to(page_filaments);
        btn_finish->SetFocus();
        break;
    default:
        index->go_to(page_welcome);
        btn_next->SetFocus();
        break;
    }
}

ConfigWizard::priv::Repository *ConfigWizard::priv::get_repo(const std::string &repo_id)
{
    auto it = std::find(repositories.begin(), repositories.end(), repo_id);
    if (it == repositories.end())
        return nullptr;
    return &repositories[it - repositories.begin()];
}

void ConfigWizard::priv::create_vendor_printers_page(const std::string &repo_id, const VendorProfile *vendor,
                                                     bool install /* = false*/,
                                                     bool from_single_vendor_repo /*= false*/)
{
    bool is_fff_technology = false;

    for (auto &model : vendor->models)
    {
        if (model.technology == ptFFF)
        {
            is_fff_technology = true;
            break;
        }
    }

    if (!is_fff_technology)
        return;

    const unsigned indent = 1;

    PagePrinters *pageFFF = new PagePrinters(q, vendor->name + " " + _L("Printers"), vendor->name, *vendor, indent);
    pageFFF->install = install;
    add_page(pageFFF);

    if (from_single_vendor_repo)
    {
        pages_fff.emplace_back(pageFFF);
    }
    else
    {
        auto repo = get_repo(repo_id);
        assert(repo);
        repo->printers_pages.insert({vendor->id, pageFFF});
    }
}

void ConfigWizard::priv::set_run_reason(RunReason run_reason)
{
    this->run_reason = run_reason;
    for (auto &page : all_pages)
    {
        page->set_run_reason(run_reason);
    }
}

void ConfigWizard::priv::update_materials()
{
    auto add_material =
        [](Materials &materials, PresetAliases &aliases, const Preset &preset, const Preset *printer = nullptr)
    {
        if (!materials.containts(&preset))
        {
            materials.push(&preset);
            if (!preset.alias.empty())
                aliases[preset.alias].emplace(&preset);
        }
        if (printer)
        {
            materials.add_printer(printer);
            materials.compatibility_counter[preset.alias].insert(printer);
        }
    };

    filaments.clear();
    aliases_fff.clear();

    // Load filaments from the standalone Filaments.ini database
    for (const auto &filament : standalone_filaments)
        add_material(filaments, aliases_fff, filament);
}

void ConfigWizard::priv::on_custom_setup(const bool custom_wanted)
{
    custom_printer_selected = custom_wanted;
    load_pages();
}

void ConfigWizard::priv::on_printer_pick(PagePrinters * /*page*/, const PrinterPickerEvent &evt)
{
    any_fff_selected = check_fff_selected();

    // Update the is_visible flag on relevant printer profiles
    for (auto &pair : bundles)
    {
        if (pair.first != evt.vendor_id)
        {
            continue;
        }

        for (auto &preset : pair.second.preset_bundle->printers)
        {
            if (preset.config.opt_string("printer_model") == evt.model_id &&
                preset.config.opt_string("printer_variant") == evt.variant_name)
            {
                preset.is_visible = evt.enable;
            }
        }

        // When a printer model is picked, but there is no material installed compatible with this printer model,
        // install default materials for selected printer model silently.
        check_and_install_missing_materials(evt.model_id);
    }

    page_filaments->clear();
    if (!any_fff_selected)
    {
        // clear all filament's info, when no one printer is selected
        filaments.clear();
        aliases_fff.clear();
    }
}

bool ConfigWizard::priv::can_finish()
{
    // Allow finishing from any page, including PageUpdateManager
    // Users can set up printers from scratch without selecting any configuration source

    // Allow finishing if no vendor printer pages are shown (fresh install scenario)
    if (pages_fff.empty() && repositories.empty())
        return true;
    // Set enabling fo "Finish" button -> there should to be selected at least one printer
    return any_fff_selected || custom_printer_selected || custom_printer_in_bundle;
}

bool ConfigWizard::priv::can_go_next()
{
    // Allow proceeding from PageUpdateManager even without selections
    // This enables fresh installs to proceed to custom printer setup
    if (index->active_page() == page_update_manager)
        return true;
    return true;
}

bool ConfigWizard::priv::can_show_next()
{
    const bool is_last = index->active_is_last();

    if (index->active_page() == page_update_manager && is_last)
        return true;

    return !is_last;
}

bool ConfigWizard::priv::on_bnt_finish()
{
    wxBusyCursor wait;

    if (!page_downloader->on_finish_downloader())
    {
        index->go_to(page_downloader);
        return false;
    }

    /* If some printers were added/deleted, but related MaterialPage wasn't activated,
     * than last changes wouldn't be updated for filaments.
     * SO, do that before check_and_install_missing_materials()
     */
    if (page_filaments)
        page_filaments->check_and_update_presets();

    // check, that there is selected at least one filament
    return check_and_install_missing_materials();
}

bool ConfigWizard::priv::check_and_install_missing_materials(const std::string & /*only_for_model_id*/)
{
    return true;
}

static std::set<std::string> get_new_added_presets(const std::map<std::string, std::string> &old_data,
                                                   const std::map<std::string, std::string> &new_data)
{
    auto get_aliases = [](const std::map<std::string, std::string> &data)
    {
        std::set<std::string> old_aliases;
        for (auto item : data)
        {
            const std::string &name = item.first;
            size_t pos = name.find("@");
            old_aliases.emplace(pos == std::string::npos ? name : name.substr(0, pos - 1));
        }
        return old_aliases;
    };

    std::set<std::string> old_aliases = get_aliases(old_data);
    std::set<std::string> new_aliases = get_aliases(new_data);
    std::set<std::string> diff;
    std::set_difference(new_aliases.begin(), new_aliases.end(), old_aliases.begin(), old_aliases.end(),
                        std::inserter(diff, diff.begin()));

    return diff;
}

static std::string get_first_added_preset(const std::map<std::string, std::string> &old_data,
                                          const std::map<std::string, std::string> &new_data)
{
    std::set<std::string> diff = get_new_added_presets(old_data, new_data);
    if (diff.empty())
        return std::string();
    return *diff.begin();
}

bool ConfigWizard::priv::apply_config(AppConfig *app_config, PresetBundle *preset_bundle,
                                      const PresetUpdaterWrapper *updater, bool &apply_keeped_changes)
{
    wxString header, caption = _L("Configuration is edited in ConfigWizard");
    const auto enabled_vendors = appconfig_new.vendors();
    const auto enabled_vendors_old = app_config->vendors();

    PrinterTechnology preferred_pt = ptAny;
    auto get_preferred_printer_technology =
        [enabled_vendors, enabled_vendors_old](const std::string &bundle_name, const Bundle &bundle)
    {
        const auto config = enabled_vendors.find(bundle_name);
        PrinterTechnology pt = ptAny;
        if (config != enabled_vendors.end())
        {
            for (const auto &model : bundle.vendor_profile->models)
            {
                if (const auto model_it = config->second.find(model.id);
                    model_it != config->second.end() && model_it->second.size() > 0)
                {
                    pt = model.technology;
                    const auto config_old = enabled_vendors_old.find(bundle_name);
                    if (config_old == enabled_vendors_old.end() ||
                        config_old->second.find(model.id) == config_old->second.end())
                        return pt;

                    if (const auto model_it_old = config_old->second.find(model.id);
                        model_it_old == config_old->second.end() || model_it_old->second != model_it->second)
                        return pt;
                }
            }
        }
        return ptAny;
    };
    // preFlight printers are considered first, then 3rd party.
    if (preferred_pt = get_preferred_printer_technology("preFlight", bundles.preflight_bundle()); preferred_pt == ptAny)
    {
        for (const auto &bundle : bundles)
        {
            if (bundle.second.is_preflight_bundle)
            {
                continue;
            }
            if (PrinterTechnology pt = get_preferred_printer_technology(bundle.first, bundle.second); pt == ptAny)
                continue;
            else if (preferred_pt == ptAny)
                preferred_pt = pt;
            if (preferred_pt != ptAny)
                break;
        }
    }

    bool check_unsaved_preset_changes = page_welcome->reset_user_profile();
    if (check_unsaved_preset_changes)
        header = _L("All user presets will be deleted.");
    int act_btns = ActionButtons::KEEP;
    if (!check_unsaved_preset_changes)
        act_btns |= ActionButtons::SAVE;

    // Vendor bundles are loaded directly from data_dir()/vendor/ - no resource/cache installation needed

#if defined(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)
    // Desktop integration on Linux
    BOOST_LOG_TRIVIAL(debug) << "ConfigWizard::priv::apply_config integrate_desktop"
                             << page_welcome->integrate_desktop() << " perform_registration_linux "
                             << DownloaderUtils::Worker::perform_registration_linux;
    if (page_welcome->integrate_desktop())
        DesktopIntegrationDialog::perform_desktop_integration();
    if (DownloaderUtils::Worker::perform_registration_linux)
        DesktopIntegrationDialog::perform_downloader_desktop_integration();
#endif //(__linux__) && defined(PREFLIGHT_DESKTOP_INTEGRATION)

    // Decide whether to create snapshot based on run_reason and the reset profile checkbox
    bool snapshot = true;
    Snapshot::Reason snapshot_reason = Snapshot::SNAPSHOT_UPGRADE;
    switch (run_reason)
    {
    case ConfigWizard::RR_DATA_EMPTY:
        snapshot = false;
        break;
    case ConfigWizard::RR_DATA_LEGACY:
        snapshot = true;
        break;
    case ConfigWizard::RR_DATA_INCOMPAT:
        // In this case snapshot has already been taken by
        // PresetUpdater with the appropriate reason
        snapshot = false;
        break;
    case ConfigWizard::RR_USER:
        snapshot = page_welcome->reset_user_profile();
        snapshot_reason = Snapshot::SNAPSHOT_USER;
        break;
    }

    if (snapshot && !take_config_snapshot_cancel_on_error(*app_config, snapshot_reason, "",
                                                          _u8L("Do you want to continue changing the configuration?")))
        return false;

    if (check_unsaved_preset_changes &&
        !wxGetApp().check_and_keep_current_preset_changes(caption, header, act_btns, &apply_keeped_changes))
        return false;

    // Vendor bundles are served from profiles.preflight3d.com and loaded directly
    // from data_dir()/vendor/. No bundle installation from resources needed.
    BOOST_LOG_TRIVIAL(info) << "Loaded " << bundles.size() << " vendor bundles from data_dir()/vendor/";

    if (page_welcome->reset_user_profile())
    {
        BOOST_LOG_TRIVIAL(info) << "Resetting user profiles...";
        preset_bundle->reset(true);
    }

    std::string preferred_model;
    std::string preferred_variant;
    auto get_preferred_printer_model =
        [enabled_vendors, enabled_vendors_old, preferred_pt](const std::string &bundle_name, const Bundle &bundle,
                                                             std::string &variant)
    {
        const auto config = enabled_vendors.find(bundle_name);
        if (config == enabled_vendors.end())
            return std::string();
        for (const auto &model : bundle.vendor_profile->models)
        {
            if (const auto model_it = config->second.find(model.id);
                model_it != config->second.end() && model_it->second.size() > 0 && preferred_pt == model.technology)
            {
                variant = *model_it->second.begin();
                const auto config_old = enabled_vendors_old.find(bundle_name);
                if (config_old == enabled_vendors_old.end())
                    return model.id;
                const auto model_it_old = config_old->second.find(model.id);
                if (model_it_old == config_old->second.end())
                    return model.id;
                else if (model_it_old->second != model_it->second)
                {
                    for (const auto &var : model_it->second)
                        if (model_it_old->second.find(var) == model_it_old->second.end())
                        {
                            variant = var;
                            return model.id;
                        }
                }
            }
        }
        if (!variant.empty())
            variant.clear();
        return std::string();
    };
    // preFlight printers are considered first, then 3rd party.
    if (preferred_model = get_preferred_printer_model("preFlight", bundles.preflight_bundle(), preferred_variant);
        preferred_model.empty())
    {
        for (const auto &bundle : bundles)
        {
            if (bundle.second.is_preflight_bundle)
            {
                continue;
            }
            if (preferred_model = get_preferred_printer_model(bundle.first, bundle.second, preferred_variant);
                !preferred_model.empty())
                break;
        }
    }

    // if unsaved changes was not cheched till this moment
    if (!check_unsaved_preset_changes)
    {
        if ((check_unsaved_preset_changes = !preferred_model.empty()))
        {
            header = _L("A new Printer was installed and it will be activated.");
            if (!wxGetApp().check_and_keep_current_preset_changes(caption, header, act_btns, &apply_keeped_changes))
                return false;
        }
        else if ((check_unsaved_preset_changes = enabled_vendors_old != enabled_vendors))
        {
            header = _L("Some Printers were uninstalled.");
            if (!wxGetApp().check_and_keep_current_preset_changes(caption, header, act_btns, &apply_keeped_changes))
                return false;
        }
    }

    std::string first_added_filament;
    auto get_first_added_material_preset =
        [this, app_config](const std::string &section_name, std::string &first_added_preset)
    {
        if (appconfig_new.has_section(section_name))
        {
            // get first of new added preset names
            const std::map<std::string, std::string> &old_presets = app_config->has_section(section_name)
                                                                        ? app_config->get_section(section_name)
                                                                        : std::map<std::string, std::string>();
            first_added_preset = get_first_added_preset(old_presets, appconfig_new.get_section(section_name));
        }
    };
    get_first_added_material_preset(AppConfig::SECTION_FILAMENTS, first_added_filament);

    // if unsaved changes was not cheched till this moment
    if (!check_unsaved_preset_changes)
    {
        if ((check_unsaved_preset_changes = !first_added_filament.empty()))
        {
            header = _L("A new filament was installed and it will be activated.");
            if (!wxGetApp().check_and_keep_current_preset_changes(caption, header, act_btns, &apply_keeped_changes))
                return false;
        }
        else
        {
            auto changed =
                [app_config, &appconfig_new = std::as_const(this->appconfig_new)](const std::string &section_name)
            {
                if (!appconfig_new.has_section(section_name))
                    return false;
                return (app_config->has_section(section_name)
                            ? app_config->get_section(section_name)
                            : std::map<std::string, std::string>()) != appconfig_new.get_section(section_name);
            };
            if ((check_unsaved_preset_changes = changed(AppConfig::SECTION_FILAMENTS)))
            {
                header = _L("Some filaments were uninstalled.");
                if (!wxGetApp().check_and_keep_current_preset_changes(caption, header, act_btns, &apply_keeped_changes))
                    return false;
            }
        }
    }

    // apply filaments in app_config
    if (appconfig_new.has_section(AppConfig::SECTION_FILAMENTS))
        app_config->set_section(AppConfig::SECTION_FILAMENTS, appconfig_new.get_section(AppConfig::SECTION_FILAMENTS));

    // Sanitize preset name for use as a filename (replace OS-invalid characters)
    auto sanitize_filename = [](const std::string &name) -> std::string
    {
        std::string out = name;
        for (char &c : out)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                c == '|')
                c = '_';
        return out;
    };

    // Save selected printer presets to data_dir()/printer/ so they persist
    std::vector<std::string> save_failures;
    {
        namespace fs = boost::filesystem;
        const fs::path printer_dir = fs::path(Luminary::data_dir()) / "printer";
        fs::create_directories(printer_dir);

        // Collect selected printer presets from wizard checkbox pages
        std::vector<const Preset *> selected_printers;
        auto collect_from_page = [&](const PagePrinters *page)
        {
            if (!page)
                return;
            for (const auto *picker : page->printer_pickers)
            {
                auto check_cbox = [&](const PrinterPicker::Checkbox *cbox)
                {
                    if (!cbox->GetValue())
                        return;
                    for (const auto &[bname, bundle] : bundles)
                        for (const auto &printer : bundle.preset_bundle->printers)
                        {
                            std::string model = printer.config.opt_string("printer_model");
                            std::string variant = printer.config.opt_string("printer_variant");
                            if (model == cbox->model && variant == cbox->variant)
                                selected_printers.push_back(&printer);
                        }
                };
                for (const auto *cbox : picker->cboxes)
                    check_cbox(cbox);
                for (const auto *cbox : picker->cboxes_alt)
                    check_cbox(cbox);
            }
        };

        for (const auto *page : pages_fff)
            collect_from_page(page);
        for (const auto &repo : repositories)
            for (const auto &[name, pages] : repo.printers_pages)
                collect_from_page(pages);

        // Check for existing files that would be overwritten
        std::vector<std::pair<const Preset *, fs::path>> conflicts;
        for (const auto *printer : selected_printers)
        {
            fs::path preset_path = printer_dir / (sanitize_filename(printer->name) + ".ini");
            if (fs::exists(preset_path))
                conflicts.emplace_back(printer, preset_path);
        }

        // Prompt once for all conflicts
        std::set<std::string> skip_overwrite;
        if (!conflicts.empty())
        {
            wxString names;
            for (const auto &[printer, path] : conflicts)
                names += "  " + from_u8(printer->name) + "\n";

            MessageDialog dlg(q,
                              _L("The following printer presets already exist and may contain custom settings. "
                                 "Overwrite them with defaults?\n\n") +
                                  names,
                              _L("Overwrite Profiles"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
            if (dlg.ShowModal() != wxID_YES)
            {
                for (const auto &[printer, path] : conflicts)
                    skip_overwrite.insert(printer->name);
            }
        }

        // Save each selected printer preset to disk
        for (const auto *printer : selected_printers)
        {
            if (skip_overwrite.count(printer->name))
                continue;
            fs::path preset_path = printer_dir / (sanitize_filename(printer->name) + ".ini");
            try
            {
                printer->config.save(preset_path.string());
                if (!fs::exists(preset_path) || fs::file_size(preset_path) == 0)
                    save_failures.push_back(printer->name);
            }
            catch (const std::exception &)
            {
                save_failures.push_back(printer->name);
            }
        }

        BOOST_LOG_TRIVIAL(info) << "Saved " << selected_printers.size() - skip_overwrite.size()
                                << " printer presets to " << printer_dir.string();

        // Print presets come from vendor bundles at runtime, filtered by compatibility
        // with installed printers. No need to save them as user .ini files.
    }

    app_config->set_vendors(appconfig_new);

    app_config->set("notify_release", page_update->version_check ? "all" : "none");
    app_config->set("export_sources_full_pathnames", page_reload_from_disk->full_pathnames ? "1" : "0");

#ifdef _WIN32
    app_config->set("associate_3mf", page_files_association->associate_3mf() ? "1" : "0");
    app_config->set("associate_stl", page_files_association->associate_stl() ? "1" : "0");
    //    app_config->set("associate_gcode", page_files_association->associate_gcode() ? "1" : "0");

    if (wxGetApp().is_editor())
    {
        if (page_files_association->associate_3mf())
            wxGetApp().associate_3mf_files();
        if (page_files_association->associate_stl())
            wxGetApp().associate_stl_files();
    }
//    else {
//        if (page_files_association->associate_gcode())
//            wxGetApp().associate_gcode_files();
//    }
#endif // _WIN32

    // page_mode->serialize_mode(app_config);

    if (check_unsaved_preset_changes)
        preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem,
                                    {preferred_model, preferred_variant, first_added_filament});

    if (page_custom->custom_wanted() && page_custom->is_valid_profile_name())
    {
        // if unsaved changes was not cheched till this moment
        if (!check_unsaved_preset_changes &&
            !wxGetApp().check_and_keep_current_preset_changes(
                caption, _L("Custom printer was installed and it will be activated."), act_btns, &apply_keeped_changes))
            return false;

        page_firmware->apply_custom_config(*custom_config);
        page_bed->apply_custom_config(*custom_config);
        page_bvolume->apply_custom_config(*custom_config);
        page_diams->apply_custom_config(*custom_config);
        page_temps->apply_custom_config(*custom_config);

        copy_bed_model_and_texture_if_needed(*custom_config);

        const std::string profile_name = page_custom->profile_name();
        preset_bundle->load_config_from_wizard(profile_name, *custom_config);
    }

    // Install selected filaments as user presets
    {
        namespace fs = boost::filesystem;
        const fs::path filament_dir = fs::path(Luminary::data_dir()) / "filament";
        fs::create_directories(filament_dir);

        // Collect checked filaments from appconfig_new (persists across filter changes)
        // plus any currently checked in the visible list (catches the current view)
        std::vector<const Preset *> selected_filaments;
        std::set<std::string> checked_names;

        // Gather from appconfig_new (selections persisted by select_material)
        if (appconfig_new.has_section(AppConfig::SECTION_FILAMENTS))
            for (const auto &[name, val] : appconfig_new.get_section(AppConfig::SECTION_FILAMENTS))
                if (val == "1")
                    checked_names.insert(name);

        // Also gather from current list view (in case select_material missed any)
        if (page_filaments)
            for (unsigned i = 0; i < page_filaments->list_profile->GetCount(); i++)
                if (page_filaments->list_profile->IsChecked(i))
                    checked_names.insert(page_filaments->list_profile->get_data(i));

        for (const auto &filament : standalone_filaments)
            if (checked_names.count(filament.name))
                selected_filaments.push_back(&filament);

        // Check for existing files that would be overwritten
        std::vector<const Preset *> conflicts;
        for (const auto *filament : selected_filaments)
            if (fs::exists(filament_dir / (sanitize_filename(filament->name) + ".ini")))
                conflicts.push_back(filament);

        // Prompt once for all conflicts
        std::set<std::string> skip_overwrite;
        if (!conflicts.empty())
        {
            wxString names;
            for (const auto *filament : conflicts)
                names += "  " + from_u8(filament->name) + "\n";

            MessageDialog dlg(q,
                              _L("The following filament presets already exist and may contain custom settings. "
                                 "Overwrite them with defaults?\n\n") +
                                  names,
                              _L("Overwrite Profiles"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
            if (dlg.ShowModal() != wxID_YES)
            {
                for (const auto *filament : conflicts)
                    skip_overwrite.insert(filament->name);
            }
        }

        // Save each selected filament preset to disk
        for (const auto *filament : selected_filaments)
        {
            if (skip_overwrite.count(filament->name))
                continue;
            fs::path preset_path = filament_dir / (sanitize_filename(filament->name) + ".ini");
            try
            {
                filament->config.save(preset_path.string());
                if (!fs::exists(preset_path) || fs::file_size(preset_path) == 0)
                    save_failures.push_back(filament->name);
            }
            catch (const std::exception &)
            {
                save_failures.push_back(filament->name);
            }
        }

        // Reload presets so the new filaments appear
        preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilent);
    }

    // Alert user if any presets failed to save
    if (!save_failures.empty())
    {
        wxString names;
        for (const auto &name : save_failures)
            names += "  " + from_u8(name) + "\n";
        MessageDialog dlg(q,
                          _L("The following presets could not be saved to disk. Check that you have write "
                             "permissions and sufficient disk space.\n\n") +
                              names,
                          _L("Save Error"), wxOK | wxICON_WARNING);
        dlg.ShowModal();
    }

    // Update the selections from the compatibilty.
    preset_bundle->export_selections(*app_config);

    return true;
}
void ConfigWizard::priv::update_presets_in_config(const std::string &section, const std::string &alias_key, bool add)
{
    auto update = [this, add](const std::string &s, const std::string &key)
    {
        assert(!s.empty());
        if (add)
            appconfig_new.set(s, key, "1");
        else
            appconfig_new.erase(s, key);
    };

    // add or delete presets had a same alias
    auto it = aliases_fff.find(alias_key);
    if (it != aliases_fff.end())
        for (const Preset *preset : it->second)
            update(section, preset->name);
}

bool ConfigWizard::priv::check_fff_selected()
{
    for (const auto page : pages_fff)
        if (page->any_selected())
            return true;

    for (const auto &repos : repositories)
        for (const auto &printers : repos.printers_pages)
            if (const auto page = printers.second; page && page->any_selected())
                return true;

    return false;
}

void ConfigWizard::priv::set_config_updated_from_archive(bool load_installed_printers, bool run_preset_updater)
{
    if (run_preset_updater && !selected_vendor_ids.empty())
    {
        // Download resources (thumbnails, bed models, textures) only for selected vendors
        int total = static_cast<int>(selected_vendor_ids.size());
        int current = 0;
        wxProgressDialog progress(_L("Downloading Resources"), _L("Preparing..."), total, q,
                                  wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH);
        progress.SetMinSize(wxSize(40 * wxGetApp().em_unit(), -1));
        progress.Fit();

        for (const auto &vendor_id : selected_vendor_ids)
        {
            progress.Update(current, format_wxstr(_L("Downloading %1% (%2% of %3%)"), vendor_id, current + 1, total));
            wxTheApp->Yield();
            download_vendor_resources(vendor_id);
            ++current;
        }
    }

    // Don't pre-select printers - wizard starts with all unchecked

    // Build printer pages for selected vendors only
    wxBusyCursor wait_cursor;
    wxWindowUpdateLocker freeze_guard(q);

    clear_printer_pages();

    const std::string repo_id = "other";
    if (std::find(repositories.begin(), repositories.end(), repo_id) == repositories.end())
        repositories.push_back({repo_id});

    // Create printer pages sorted alphabetically by vendor name
    std::vector<std::pair<std::string, std::string>> sorted_vendors; // {lowercase_name, vendor_id}
    for (const auto &vendor_id : selected_vendor_ids)
    {
        auto it = bundles.find(vendor_id);
        if (it == bundles.end() || !it->second.vendor_profile)
            continue;
        sorted_vendors.emplace_back(boost::algorithm::to_lower_copy(it->second.vendor_profile->name), vendor_id);
    }
    std::sort(sorted_vendors.begin(), sorted_vendors.end());

    for (const auto &[sort_key, vendor_id] : sorted_vendors)
    {
        auto it = bundles.find(vendor_id);
        create_vendor_printers_page(repo_id, it->second.vendor_profile, true, true);
    }

    if (!page_custom)
    {
        add_page(page_custom = new PageCustom(q));
        custom_printer_selected = page_custom->custom_wanted();
    }

    any_fff_selected = check_fff_selected();

    if (!page_filaments)
        add_page(page_filaments = new PageMaterials(q, &filaments, _L("Filament Profiles Selection"), _L("Filaments"),
                                                    _L("Type:")));

    check_and_install_missing_materials();
    update_materials();
    if (page_filaments)
        page_filaments->reload_presets();

    load_pages();
}

static bool to_delete(PagePrinters *page, const std::set<std::string> &selected_uuids)
{
    const SharedArchiveRepositoryVector &archs = wxGetApp().get_preset_updater_wrapper()->get_all_archive_repositories();

    bool unselect_all = true;

    for (const auto &archive : archs)
    {
        if (page->get_vendor_repo_id() == archive->get_manifest().id)
        {
            if (selected_uuids.find(archive->get_uuid()) != selected_uuids.end())
                unselect_all = false;
            //break; ! don't break here, because there can be several archives with same repo_id
        }
    }
    return unselect_all;
}

static void unselect(PagePrinters *page)
{
    const Luminary::PresetUpdaterWrapper *puw = wxGetApp().get_preset_updater_wrapper();
    const SharedArchiveRepositoryVector &archs = puw->get_all_archive_repositories();

    bool unselect_all = true;

    for (const auto *archive : archs)
    {
        if (page->get_vendor_repo_id() == archive->get_manifest().id)
        {
            if (puw->is_selected_repository_by_uuid(archive->get_uuid()))
                unselect_all = false;
            //break; ! don't break here, because there can be several archives with same repo_id
        }
    }

    if (unselect_all)
        page->unselect_all_presets();
}

bool ConfigWizard::priv::can_clear_printer_pages()
{
    return true;
}

void ConfigWizard::priv::clear_printer_pages()
{
    auto delelete_page = [this](PagePrinters *page)
    {
        // unselect page to correct process those changes in app_config
        unselect(page);

        // remove page
        hscroll->GetContentPanel()->RemoveChild(
            page); // Under OSX call of Reparent(nullptr) causes a crash, so as a workaround use RemoveChild() instead
        page->Destroy();
    };

    for (PagePrinters *page : pages_fff)
        delelete_page(page);
    pages_fff.clear();

    for (Repository &repo : repositories)
    {
        if (!repo.vendors_page)
            continue;
        for (auto &[name, printers] : repo.printers_pages)
        {
            if (printers)
                delelete_page(printers);
        }
    }
    repositories.clear();
}

bool ConfigWizard::priv::installed_multivendors_repos()
{
    for (const auto &repo : repositories)
        if (repo.vendors_page)
            return true;
    return false;
}

// Public

ConfigWizard::ConfigWizard(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, wxString(PREFLIGHT_APP_NAME) + " - " + _(name()), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , p(new priv(this))
{
#ifndef _WIN32
    // Apply theme background on Linux/macOS (UpdateDarkUI is Windows-only)
    this->SetBackgroundColour(wxGetApp().get_window_default_clr());
#endif
    wxBusyCursor wait;

    this->SetFont(wxGetApp().normal_font());

    p->load_vendors();
    p->custom_config.reset(DynamicPrintConfig::new_from_defaults_keys({
        "gcode_flavor",
        "bed_shape",
        "bed_custom_texture",
        "bed_custom_model",
        "nozzle_diameter",
        "filament_diameter",
        "temperature",
        "bed_temperature",
    }));

    p->index = new ConfigWizardIndex(this);

    auto *vsizer = new wxBoxSizer(wxVERTICAL);
    auto *topsizer = new wxBoxSizer(wxHORIZONTAL);
    auto *hline = new StaticLine(this);
    p->btnsizer = new wxBoxSizer(wxHORIZONTAL);

    p->hscroll = new ScrollablePanel(this);
    p->hscroll_sizer = new wxBoxSizer(wxHORIZONTAL);
    p->hscroll->SetSizer(p->hscroll_sizer);

    topsizer->Add(p->index, 0, wxEXPAND);
    topsizer->AddSpacer(GetScaledIndexMargin());
    topsizer->Add(p->hscroll, 1, wxEXPAND);

    p->btn_prev = new wxButton(this, wxID_ANY, _L("< &Back"));
    p->btn_next = new wxButton(this, wxID_ANY, _L("&Next >"));
    p->btn_finish = new wxButton(this, wxID_APPLY, _L("&Finish"));
    p->btn_cancel =
        new wxButton(this, wxID_CANCEL,
                     _L("Cancel")); // Note: The label needs to be present, otherwise we get accelerator bugs on Mac
    p->btnsizer->AddStretchSpacer();
    p->btnsizer->Add(p->btn_prev, 0, wxLEFT, GetScaledBtnSpacing());
    p->btnsizer->Add(p->btn_next, 0, wxLEFT, GetScaledBtnSpacing());
    p->btnsizer->Add(p->btn_finish, 0, wxLEFT, GetScaledBtnSpacing());
    p->btnsizer->Add(p->btn_cancel, 0, wxLEFT, GetScaledBtnSpacing());

    wxGetApp().UpdateDarkUI(p->btn_prev);
    wxGetApp().UpdateDarkUI(p->btn_next);
    wxGetApp().UpdateDarkUI(p->btn_finish);
    wxGetApp().UpdateDarkUI(p->btn_cancel);

    wxGetApp().SetWindowVariantForButton(p->btn_prev);
    wxGetApp().SetWindowVariantForButton(p->btn_next);
    wxGetApp().SetWindowVariantForButton(p->btn_finish);
    wxGetApp().SetWindowVariantForButton(p->btn_cancel);

    p->add_page(p->page_welcome = new PageWelcome(this));
    p->add_page(p->page_update_manager = new PageUpdateManager(this));
    p->page_update_manager->populate_vendor_list();

    // other pages will be loaded later after confirm repositories selection

    p->load_pages();
    p->index->go_to(size_t{0});

    p->add_page(p->page_update = new PageUpdate(this));
    p->add_page(p->page_downloader = new PageDownloader(this));
    p->add_page(p->page_reload_from_disk = new PageReloadFromDisk(this));
#ifdef _WIN32
    p->add_page(p->page_files_association = new PageFilesAssociation(this));
#endif // _WIN32
    p->add_page(p->page_mode = new PageMode(this));
    p->add_page(p->page_firmware = new PageFirmware(this));
    p->add_page(p->page_bed = new PageBedShape(this));
    p->add_page(p->page_bvolume = new PageBuildVolume(this));
    p->add_page(p->page_diams = new PageDiameters(this));
    p->add_page(p->page_temps = new PageTemperatures(this));

    vsizer->Add(topsizer, 1, wxEXPAND | wxALL, GetScaledDialogMargin());
    vsizer->Add(hline, 0, wxEXPAND | wxLEFT | wxRIGHT, GetScaledVerticalSpacing());
    vsizer->Add(p->btnsizer, 0, wxEXPAND | wxALL, GetScaledDialogMargin());

    SetSizer(vsizer);
    SetSizerAndFit(vsizer);

    p->hscroll->UpdateScrollbar();

    on_window_geometry(this, [this]() { p->init_dialog_size(); });

    p->btn_prev->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) { this->p->index->go_prev(); });

    p->btn_prev->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent &evt) { evt.Enable(p->can_go_next()); });

    p->btn_next->Bind(wxEVT_BUTTON,
                      [this](const wxCommandEvent &)
                      {
                          // check, that there is selected at least one filament
                          ConfigWizardPage *active_page = this->p->index->active_page();
                          if ( // Leaving the filaments page and
                              active_page == p->page_filaments &&
                              // some Printer models had no filament selected.
                              !p->check_and_install_missing_materials())
                              // In that case don't leave the page and the function above queried the user whether to install default materials.
                              return;
                          if (active_page == p->page_update_manager && p->index->active_is_last())
                          {
                              size_t next_active = p->index->pages_cnt();
                              p->page_update_manager->Hide();
                              p->index->go_to(next_active);
                              return;
                          }
                          this->p->index->go_next();
                      });

    p->btn_next->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent &evt) { evt.Enable(p->can_go_next()); });

    p->btn_finish->Bind(wxEVT_BUTTON,
                        [this](const wxCommandEvent &)
                        {
                            if (p->on_bnt_finish())
                                this->EndModal(wxID_OK);
                        });

    p->btn_finish->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent &evt) { evt.Enable(p->can_finish()); });

    p->index->Bind(EVT_INDEX_PAGE,
                   [this](const wxCommandEvent &)
                   {
                       p->btn_next->Show(p->can_show_next());

                       if (p->index->active_is_last())
                           p->btn_finish->SetFocus();
                       Layout();
                   });

    wxGetApp().UpdateDlgDarkUI(this);
}

ConfigWizard::~ConfigWizard() {}

bool ConfigWizard::run(RunReason reason, StartPage start_page)
{
    BOOST_LOG_TRIVIAL(info) << boost::format("Running ConfigWizard, reason: %1%, start_page: %2%") % reason %
                                   start_page;

    GUI_App &app = wxGetApp();

    p->set_run_reason(reason);
    p->set_start_page(start_page);
    p->is_config_from_archive = reason == RR_USER;

    // Restore previously selected vendors so printer pages are pre-generated on launch
    {
        const std::string saved = wxGetApp().app_config->get("selected_vendors");
        if (!saved.empty())
        {
            std::istringstream ss(saved);
            std::string token;
            while (std::getline(ss, token, ';'))
                if (!token.empty())
                    p->selected_vendor_ids.insert(token);
        }
    }
    p->set_config_updated_from_archive(p->is_config_from_archive, false);

    if (ShowModal() == wxID_OK)
    {
        bool apply_keeped_changes = false;
        if (!p->apply_config(app.app_config, app.preset_bundle, app.get_preset_updater_wrapper(), apply_keeped_changes))
            return false;

        if (apply_keeped_changes)
            app.apply_keeped_preset_modifications();

        app.app_config->set_legacy_datadir(false);
        app.update_mode();
        app.obj_manipul()->update_ui_from_settings();
        BOOST_LOG_TRIVIAL(info) << "ConfigWizard applied";
        return true;
    }
    else
    {
        BOOST_LOG_TRIVIAL(info) << "ConfigWizard cancelled";
        return false;
    }
}

void ConfigWizard::update_login() {}

const wxString &ConfigWizard::name(const bool from_menu /* = false*/)
{
    // A different naming convention is used for the Wizard on Windows & GTK vs. OSX.
    // Note: Don't call _() macro here.
    //       This function just return the current name according to the OS.
    //       Translation is implemented inside GUI_App::add_config_menu()
#if __APPLE__
    static const wxString config_wizard_name = L("Configuration Assistant");
    static const wxString config_wizard_name_menu = L("Configuration &Assistant");
#else
    static const wxString config_wizard_name = L("Configuration Wizard");
    static const wxString config_wizard_name_menu = L("Configuration &Wizard");
#endif
    return from_menu ? config_wizard_name_menu : config_wizard_name;
}

void ConfigWizard::on_dpi_changed(const wxRect &suggested_rect)
{
    p->index->msw_rescale();

    const int em = em_unit();

    msw_buttons_rescale(this, em, {wxID_APPLY, wxID_CANCEL, p->btn_next->GetId(), p->btn_prev->GetId()});

    for (auto page : p->pages_fff)
        for (auto printer_picker : page->printer_pickers)
            msw_buttons_rescale(this, em, printer_picker->get_button_indexes());

    p->init_dialog_size();

    Refresh();
}

void ConfigWizard::on_sys_color_changed()
{
    wxGetApp().UpdateDlgDarkUI(this);
    Refresh();
}

} // namespace DSKY
