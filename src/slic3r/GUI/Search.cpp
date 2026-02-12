///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2023 Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Search.hpp"

#include <cstddef>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/nowide/convert.hpp>

#include "wx/numformatter.h"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "MainFrame.hpp"
#include "Tab.hpp"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"

#include "imgui/imconfig.h"
#include "Widgets/ScrollBar.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/UIColors.hpp"

#include <wx/dcbuffer.h>

using boost::optional;

namespace Slic3r
{

wxDEFINE_EVENT(wxCUSTOMEVT_JUMP_TO_OPTION, wxCommandEvent);

using GUI::from_u8;
using GUI::into_u8;

namespace Search
{

static char marker_by_type(Preset::Type type, PrinterTechnology pt)
{
    switch (type)
    {
    case Preset::TYPE_PRINT:
    case Preset::TYPE_SLA_PRINT:
        return ImGui::PrintIconMarker;
    case Preset::TYPE_FILAMENT:
        return ImGui::FilamentIconMarker;
    case Preset::TYPE_SLA_MATERIAL:
        return ImGui::MaterialIconMarker;
    case Preset::TYPE_PRINTER:
        return pt == ptSLA ? ImGui::PrinterSlaIconMarker : ImGui::PrinterIconMarker;
    case Preset::TYPE_PREFERENCES:
        return ImGui::PreferencesButton;
    default:
        return ' ';
    }
}

std::string Option::opt_key() const
{
    return into_u8(key).substr(2);
}

void FoundOption::get_marked_label_and_tooltip(const char **label_, const char **tooltip_) const
{
    *label_ = marked_label.c_str();
    *tooltip_ = tooltip.c_str();
}

template<class T>
//void change_opt_key(std::string& opt_key, DynamicPrintConfig* config)
void change_opt_key(std::string &opt_key, DynamicPrintConfig *config, int &cnt)
{
    T *opt_cur = static_cast<T *>(config->option(opt_key));
    cnt = opt_cur->values.size();
    return;

    if (opt_cur->values.size() > 0)
        opt_key += "#" + std::to_string(0);
}

static std::string get_key(const std::string &opt_key, Preset::Type type)
{
    return std::to_string(int(type)) + ";" + opt_key;
}

void OptionsSearcher::append_options(DynamicPrintConfig *config, Preset::Type type)
{
    auto emplace = [this, type](std::string key, const wxString &label, int id = -1)
    {
        if (id >= 0)
            // ! It's very important to use "#". opt_key#n is a real option key used in GroupAndCategory
            key += "#" + std::to_string(id);

        const GroupAndCategory &gc = groups_and_categories[key];
        if (gc.group.IsEmpty() || gc.category.IsEmpty())
            return;

        wxString suffix;
        wxString suffix_local;
        if (gc.category == "Machine limits" || gc.category == "Material printing profile")
        {
            if (gc.category == "Machine limits")
                suffix = id == 1 ? L("Stealth") : L("Normal");
            else
                suffix = id == 1 ? L("Above") : L("Below");
            suffix_local = " " + _(suffix);
            suffix = " " + suffix;
        }
        else if (gc.group == "Dynamic overhang speed" && id >= 0)
        {
            suffix = " " + std::to_string(id + 1);
            suffix_local = suffix;
        }

        if (!label.IsEmpty())
            options.emplace_back(Option{boost::nowide::widen(key), type, (label + suffix).ToStdWstring(),
                                        (_(label) + suffix_local).ToStdWstring(), gc.group.ToStdWstring(),
                                        _(gc.group).ToStdWstring(), gc.category.ToStdWstring(),
                                        GUI::Tab::translate_category(gc.category, type).ToStdWstring()});
    };

    for (std::string opt_key : config->keys())
    {
        const ConfigOptionDef &opt = *config->option_def(opt_key);
        if (opt.mode > mode)
            continue;

        int cnt = 0;

        if (type != Preset::TYPE_FILAMENT && !PresetCollection::is_independent_from_extruder_number_option(opt_key))
            switch (config->option(opt_key)->type())
            {
            case coInts:
                change_opt_key<ConfigOptionInts>(opt_key, config, cnt);
                break;
            case coBools:
                change_opt_key<ConfigOptionBools>(opt_key, config, cnt);
                break;
            case coFloats:
                change_opt_key<ConfigOptionFloats>(opt_key, config, cnt);
                break;
            case coStrings:
                change_opt_key<ConfigOptionStrings>(opt_key, config, cnt);
                break;
            case coPercents:
                change_opt_key<ConfigOptionPercents>(opt_key, config, cnt);
                break;
            case coPoints:
                change_opt_key<ConfigOptionPoints>(opt_key, config, cnt);
                break;
            case coFloatsOrPercents:
                change_opt_key<ConfigOptionFloatsOrPercents>(opt_key, config, cnt);
                break;
            case coEnums:
                change_opt_key<ConfigOptionEnumsGeneric>(opt_key, config, cnt);
                break;

            default:
                break;
            }

        wxString label = opt.full_label.empty() ? opt.label : opt.full_label;

        std::string key = get_key(opt_key, type);
        if (cnt == 0)
            emplace(key, label);
        else
            for (int i = 0; i < cnt; ++i)
                emplace(key, label, i);
    }
}

// Mark a string using ColorMarkerStart and ColorMarkerEnd symbols
static std::wstring mark_string(const std::wstring &str, const std::vector<uint16_t> &matches, Preset::Type type,
                                PrinterTechnology pt)
{
    std::wstring out;
    out += marker_by_type(type, pt);
    if (matches.empty())
        out += str;
    else
    {
        out.reserve(str.size() * 2);
        if (matches.front() > 0)
            out += str.substr(0, matches.front());
        for (size_t i = 0;;)
        {
            // Find the longest string of successive indices.
            size_t j = i + 1;
            while (j < matches.size() && matches[j] == matches[j - 1] + 1)
                ++j;
            out += ImGui::ColorMarkerStart;
            out += str.substr(matches[i], matches[j - 1] - matches[i] + 1);
            out += ImGui::ColorMarkerEnd;
            if (j == matches.size())
            {
                out += str.substr(matches[j - 1] + 1);
                break;
            }
            out += str.substr(matches[j - 1] + 1, matches[j] - matches[j - 1] - 1);
            i = j;
        }
    }
    return out;
}

bool OptionsSearcher::search()
{
    return search(search_line, true);
}

static bool fuzzy_match(const std::wstring &search_pattern, const std::wstring &label, int &out_score,
                        std::vector<uint16_t> &out_matches)
{
    uint16_t matches[fts::max_matches + 1]; // +1 for the stopper
    int score;
    if (fts::fuzzy_match(search_pattern.c_str(), label.c_str(), score, matches))
    {
        size_t cnt = 0;
        for (; matches[cnt] != fts::stopper; ++cnt)
            ;
        out_matches.assign(matches, matches + cnt);
        out_score = score;
        return true;
    }
    else
        return false;
}

bool OptionsSearcher::search(const std::string &search, bool force /* = false*/)
{
    if (search_line == search && !force)
        return false;

    found.clear();

    bool full_list = search.empty();
    std::wstring sep = L" : ";

    auto get_label = [this, &sep](const Option &opt, bool marked = true)
    {
        std::wstring out;
        if (marked)
            out += marker_by_type(opt.type, printer_technology);
        const std::wstring *prev = nullptr;
        for (const std::wstring *const s :
             {view_params.category ? &opt.category_local : nullptr, &opt.group_local, &opt.label_local})
            if (s != nullptr && (prev == nullptr || *prev != *s))
            {
                if (out.size() > 2)
                    out += sep;
                out += *s;
                prev = s;
            }
        return out;
    };

    auto get_label_english = [this, &sep](const Option &opt, bool marked = true)
    {
        std::wstring out;
        if (marked)
            out += marker_by_type(opt.type, printer_technology);
        const std::wstring *prev = nullptr;
        for (const std::wstring *const s : {view_params.category ? &opt.category : nullptr, &opt.group, &opt.label})
            if (s != nullptr && (prev == nullptr || *prev != *s))
            {
                if (out.size() > 2)
                    out += sep;
                out += *s;
                prev = s;
            }
        return out;
    };

    auto get_tooltip = [this, &sep](const Option &opt) -> wxString
    {
        return marker_by_type(opt.type, printer_technology) + opt.category_local + sep + opt.group_local + sep +
               opt.label_local;
    };

    std::vector<uint16_t> matches, matches2;
    for (size_t i = 0; i < options.size(); i++)
    {
        const Option &opt = options[i];
        if (full_list)
        {
            std::string label = into_u8(get_label(opt));
            found.emplace_back(FoundOption{label, label, into_u8(get_tooltip(opt)), i, 0});
            continue;
        }

        std::wstring wsearch = boost::nowide::widen(search);
        boost::trim_left(wsearch);
        std::wstring label = get_label(opt, false);
        std::wstring label_english = get_label_english(opt, false);
        int score = std::numeric_limits<int>::min();
        int score2;
        matches.clear();
        fuzzy_match(wsearch, label, score, matches);
        if (fuzzy_match(wsearch, opt.key, score2, matches2) && score2 > score)
        {
            for (fts::pos_type &pos : matches2)
                pos += label.size() + 1;
            label += L"(" + opt.key + L")";
            append(matches, matches2);
            score = score2;
        }
        if (view_params.english && fuzzy_match(wsearch, label_english, score2, matches2) && score2 > score)
        {
            label = std::move(label_english);
            matches = std::move(matches2);
            score = score2;
        }
        if (score > 90 /*std::numeric_limits<int>::min()*/)
        {
            label = mark_string(label, matches, opt.type, printer_technology);
            label += L"  [" + std::to_wstring(score) + L"]"; // add score value
            std::string label_u8 = into_u8(label);
            std::string label_plain = label_u8;

#ifdef SUPPORTS_MARKUP
            boost::replace_all(label_plain, std::string(1, char(ImGui::ColorMarkerStart)), "<b>");
            boost::replace_all(label_plain, std::string(1, char(ImGui::ColorMarkerEnd)), "</b>");
#else
            boost::erase_all(label_plain, std::string(1, char(ImGui::ColorMarkerStart)));
            boost::erase_all(label_plain, std::string(1, char(ImGui::ColorMarkerEnd)));
#endif
            found.emplace_back(FoundOption{label_plain, label_u8, into_u8(get_tooltip(opt)), i, score});
        }
    }

    if (!full_list)
        sort_found();

    if (search_line != search)
        search_line = search;

    return true;
}

OptionsSearcher::OptionsSearcher()
{
    default_string = _L("Enter a search term");
}

OptionsSearcher::~OptionsSearcher() {}

void OptionsSearcher::check_and_update(PrinterTechnology pt_in, ConfigOptionMode mode_in,
                                       std::vector<InputInfo> input_values)
{
    if (printer_technology == pt_in && mode == mode_in)
        return;

    options.clear();

    printer_technology = pt_in;
    mode = mode_in;

    for (auto i : input_values)
        append_options(i.config, i.type);

    options.insert(options.end(), preferences_options.begin(), preferences_options.end());

    sort_options();

    search(search_line, true);
}

void OptionsSearcher::append_preferences_option(const GUI::Line &opt_line)
{
    Preset::Type type = Preset::TYPE_PREFERENCES;
    wxString label = opt_line.label;
    if (label.IsEmpty())
        return;

    std::string key = get_key(opt_line.get_options().front().opt_id, type);
    const GroupAndCategory &gc = groups_and_categories[key];
    if (gc.group.IsEmpty() || gc.category.IsEmpty())
        return;

    preferences_options.emplace_back(Search::Option{boost::nowide::widen(key), type, label.ToStdWstring(),
                                                    _(label).ToStdWstring(), gc.group.ToStdWstring(),
                                                    _(gc.group).ToStdWstring(), gc.category.ToStdWstring(),
                                                    _(gc.category).ToStdWstring()});
}

void OptionsSearcher::append_preferences_options(const std::vector<GUI::Line> &opt_lines)
{
    for (const GUI::Line &line : opt_lines)
    {
        if (line.is_separator())
            continue;
        append_preferences_option(line);
    }
}

const Option &OptionsSearcher::get_option(size_t pos_in_filter) const
{
    assert(pos_in_filter != size_t(-1) && found[pos_in_filter].option_idx != size_t(-1));
    return options[found[pos_in_filter].option_idx];
}

const Option &OptionsSearcher::get_option(const std::string &opt_key, Preset::Type type) const
{
    auto it = std::lower_bound(options.begin(), options.end(), Option({boost::nowide::widen(get_key(opt_key, type))}));
    assert(it != options.end());

    return options[it - options.begin()];
}

static Option create_option(const std::string &opt_key, const wxString &label, Preset::Type type,
                            const GroupAndCategory &gc)
{
    wxString suffix;
    wxString suffix_local;
    if (gc.category == "Machine limits")
    {
        suffix = opt_key.back() == '1' ? L("Stealth") : L("Normal");
        suffix_local = " " + _(suffix);
        suffix = " " + suffix;
    }

    wxString category = gc.category;
    if (type == Preset::TYPE_PRINTER && category.Contains("Extruder "))
    {
        std::string opt_idx = opt_key.substr(opt_key.find("#") + 1);
        category = wxString::Format("%s %d", "Extruder", atoi(opt_idx.c_str()) + 1);
    }

    return Option{boost::nowide::widen(get_key(opt_key, type)),
                  type,
                  (label + suffix).ToStdWstring(),
                  (_(label) + suffix_local).ToStdWstring(),
                  gc.group.ToStdWstring(),
                  _(gc.group).ToStdWstring(),
                  gc.category.ToStdWstring(),
                  GUI::Tab::translate_category(category, type).ToStdWstring()};
}

Option OptionsSearcher::get_option(const std::string &opt_key, const wxString &label, Preset::Type type) const
{
    std::string key = get_key(opt_key, type);
    auto it = std::lower_bound(options.begin(), options.end(), Option({boost::nowide::widen(key)}));
    if (it->key == boost::nowide::widen(key))
        return options[it - options.begin()];
    if (groups_and_categories.find(key) == groups_and_categories.end())
    {
        size_t pos = key.find('#');
        if (pos == std::string::npos)
            return options[it - options.begin()];

        std::string zero_opt_key = key.substr(0, pos + 1) + "0";

        if (groups_and_categories.find(zero_opt_key) == groups_and_categories.end())
            return options[it - options.begin()];

        return create_option(opt_key, label, type, groups_and_categories.at(zero_opt_key));
    }

    const GroupAndCategory &gc = groups_and_categories.at(key);
    if (gc.group.IsEmpty() || gc.category.IsEmpty())
        return options[it - options.begin()];

    return create_option(opt_key, label, type, gc);
}

static bool has_focus(wxWindow *win)
{
    if (win->HasFocus())
        return true;

    auto children = win->GetChildren();
    for (auto child : children)
    {
        if (has_focus(child))
            return true;
    }

    return false;
}

void OptionsSearcher::update_dialog_position()
{
    // preFlight: dialog is centered on parent at creation and user-movable via title bar;
    // no need to reposition it on every show.
}

void OptionsSearcher::check_and_hide_dialog()
{
#ifdef __linux__
    // Temporary linux specific workaround:
    // has_focus(search_dialog) always returns false
    // That's why search dialog will be hidden whole the time
    return;
#endif
    if (search_dialog && search_dialog->IsShown() && !has_focus(search_dialog))
        show_dialog(false);
}

void OptionsSearcher::set_focus_to_parent()
{
    if (search_input)
        search_input->GetParent()->SetFocus();
}

void OptionsSearcher::show_dialog(bool show /*= true*/)
{
    if (search_dialog && !show)
    {
        search_dialog->Hide();
        return;
    }

    if (!search_dialog)
    {
        search_dialog = new SearchDialog(this, search_input);

        search_dialog->Bind(wxEVT_KILL_FOCUS,
                            [this](wxFocusEvent &e)
                            {
                                if (search_dialog->IsShown() && !search_input->HasFocus())
                                    show_dialog(false);
                                e.Skip();
                            });
    }
    update_dialog_position();

    search_string();

    // preFlight: the dialog has its own filter input that receives focus in Popup()
    search_dialog->Popup();
}

void OptionsSearcher::dlg_sys_color_changed()
{
    if (search_dialog)
        search_dialog->on_sys_color_changed();
}

void OptionsSearcher::dlg_msw_rescale()
{
    if (search_dialog)
        search_dialog->msw_rescale();
}

void OptionsSearcher::edit_search_input()
{
    if (!search_input)
        return;

    if (search_dialog && search_dialog->IsShown())
    {
        search_dialog->input_text(search_input->GetValue());
    }
    else
    {
        GUI::wxGetApp().show_search_dialog();
    }
}

void OptionsSearcher::process_key_down_from_input(wxKeyEvent &e)
{
    int key = e.GetKeyCode();
    if (key == WXK_ESCAPE)
    {
        set_focus_to_parent();
        search_dialog->Hide();
    }
    else if (search_dialog && (key == WXK_UP || key == WXK_DOWN || key == WXK_NUMPAD_ENTER || key == WXK_RETURN))
    {
        search_dialog->KeyDown(e);
    }
}

void OptionsSearcher::set_search_input(TextInput *input_ctrl)
{
    search_input = input_ctrl;
    update_dialog_position();
}

void OptionsSearcher::add_key(const std::string &opt_key, Preset::Type type, const wxString &group,
                              const wxString &category)
{
    groups_and_categories[get_key(opt_key, type)] = GroupAndCategory{group, category};
}

//------------------------------------------
//          SearchResultsPanel
//------------------------------------------

// preFlight: Owner-drawn search results list with custom ScrollBar.
// Replaces wxDataViewCtrl + SearchListModel for consistent warm-themed scrollbars.

// Maps icon marker characters to icon indices 0-5
static const std::map<const char, int> icon_idxs = {
    {ImGui::PrintIconMarker, 0},    {ImGui::PrinterIconMarker, 1},  {ImGui::PrinterSlaIconMarker, 2},
    {ImGui::FilamentIconMarker, 3}, {ImGui::MaterialIconMarker, 4}, {ImGui::PreferencesButton, 5},
};

struct SearchResultRow
{
    int icon_index{0};
    wxString display_text;
    std::vector<std::pair<size_t, size_t>> highlight_ranges; // (start, length) pairs
};

class SearchResultsPanel : public wxPanel
{
public:
    SearchResultsPanel(wxWindow *parent, ScrollBar *scrollbar)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
        , m_scrollbar(scrollbar)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        Bind(wxEVT_PAINT, &SearchResultsPanel::OnPaint, this);
        Bind(wxEVT_SIZE, &SearchResultsPanel::OnSize, this);
        Bind(wxEVT_MOUSEWHEEL, &SearchResultsPanel::OnMouseWheel, this);
        Bind(wxEVT_MOTION, &SearchResultsPanel::OnMotion, this);
        Bind(wxEVT_LEFT_DOWN, &SearchResultsPanel::OnLeftDown, this);
        Bind(wxEVT_LEAVE_WINDOW, &SearchResultsPanel::OnLeaveWindow, this);

        if (m_scrollbar)
        {
            m_scrollbar->Bind(wxEVT_SCROLL_THUMBTRACK, &SearchResultsPanel::OnScroll, this);
            m_scrollbar->Bind(wxEVT_SCROLL_THUMBRELEASE, &SearchResultsPanel::OnScroll, this);
        }
    }

    void SetItems(const std::vector<FoundOption> &found_options, ScalableBitmap icons[6])
    {
        m_icons = icons;
        m_rows.clear();
        m_rows.reserve(found_options.size());

        for (const FoundOption &opt : found_options)
        {
            SearchResultRow row;
            ParseMarkedLabel(opt.marked_label, row);
            m_rows.push_back(std::move(row));
        }

        m_selected = m_rows.empty() ? -1 : 0;
        m_hovered = -1;
        m_scroll_offset = 0;
        UpdateScrollbar();
        Refresh();
    }

    void Clear()
    {
        m_rows.clear();
        m_selected = -1;
        m_hovered = -1;
        m_scroll_offset = 0;
        UpdateScrollbar();
        Refresh();
    }

    int GetSelection() const { return m_selected; }

    void SetSelection(int index)
    {
        if (index >= 0 && index < static_cast<int>(m_rows.size()))
        {
            m_selected = index;
            EnsureVisible(index);
            Refresh();
        }
    }

    void SelectNext()
    {
        if (m_selected < static_cast<int>(m_rows.size()) - 1)
            SetSelection(m_selected + 1);
    }

    void SelectPrev()
    {
        if (m_selected > 0)
            SetSelection(m_selected - 1);
    }

    int GetItemCount() const { return static_cast<int>(m_rows.size()); }

    void sys_color_changed()
    {
        SetBackgroundColour(UIColors::InputBackground());
        Refresh();
    }

    void msw_rescale()
    {
        UpdateScrollbar();
        Refresh();
    }

private:
    int RowHeight() const { return static_cast<int>(GUI::wxGetApp().em_unit() * 1.8); }

    int HitTest(const wxPoint &pos) const
    {
        if (m_rows.empty())
            return -1;
        int row = (pos.y + m_scroll_offset) / RowHeight();
        if (row < 0 || row >= static_cast<int>(m_rows.size()))
            return -1;
        return row;
    }

    void EnsureVisible(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_rows.size()))
            return;

        const int rowH = RowHeight();
        const int visibleHeight = GetClientSize().y;
        const int rowTop = index * rowH;
        const int rowBottom = rowTop + rowH;

        if (rowTop < m_scroll_offset)
            m_scroll_offset = rowTop;
        else if (rowBottom > m_scroll_offset + visibleHeight)
            m_scroll_offset = rowBottom - visibleHeight;

        if (m_scrollbar)
            m_scrollbar->SetThumbPosition(m_scroll_offset);
    }

    void UpdateScrollbar()
    {
        if (!m_scrollbar)
            return;

        const int totalHeight = static_cast<int>(m_rows.size()) * RowHeight();
        const int visibleHeight = GetClientSize().y;

        if (totalHeight > visibleHeight && visibleHeight > 0)
        {
            m_scrollbar->SetScrollbar(m_scroll_offset, visibleHeight, totalHeight, visibleHeight);
            m_scrollbar->Show();
        }
        else
        {
            m_scrollbar->Hide();
            m_scroll_offset = 0;
        }
    }

    void ParseMarkedLabel(const std::string &marked_label, SearchResultRow &row)
    {
        if (marked_label.empty())
            return;

        // First character is the icon marker
        const char icon_c = marked_label[0];
        auto it = icon_idxs.find(icon_c);
        row.icon_index = (it != icon_idxs.end()) ? it->second : 0;

        // Parse remaining text, extracting highlight ranges from ColorMarkerStart/End
        row.display_text.clear();
        row.highlight_ranges.clear();

        bool in_highlight = false;
        size_t highlight_start = 0;

        for (size_t i = 1; i < marked_label.size(); ++i)
        {
            char c = marked_label[i];
            if (c == ImGui::ColorMarkerStart)
            {
                in_highlight = true;
                highlight_start = row.display_text.length();
            }
            else if (c == ImGui::ColorMarkerEnd)
            {
                if (in_highlight)
                {
                    size_t len = row.display_text.length() - highlight_start;
                    if (len > 0)
                        row.highlight_ranges.emplace_back(highlight_start, len);
                    in_highlight = false;
                }
            }
            else
            {
                row.display_text += c;
            }
        }
    }

    // --- Event handlers ---

    void OnPaint(wxPaintEvent &event)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize clientSize = GetClientSize();
        bool is_dark = GUI::wxGetApp().dark_mode();

        wxColour bgColor = UIColors::InputBackground();
        dc.SetBackground(wxBrush(bgColor));
        dc.Clear();

        if (m_rows.empty())
            return;

        const int rowH = RowHeight();
        const int em = GUI::wxGetApp().em_unit();
        const int iconAreaWidth = em * 2;
        const int textLeftMargin = em / 2;
        const int leftPadding = em / 2;

        // Visible row range
        int firstVisible = m_scroll_offset / rowH;
        int lastVisible = (m_scroll_offset + clientSize.y + rowH - 1) / rowH;
        firstVisible = std::max(0, firstVisible);
        lastVisible = std::min(lastVisible, static_cast<int>(m_rows.size()) - 1);

        dc.SetFont(GetFont().IsOk() ? GetFont() : GUI::wxGetApp().normal_font());

        wxColour normalTextColor = UIColors::InputForeground();
        wxColour highlightTextColor = UIColors::AccentPrimary();
        wxColour selectedBg = UIColors::HighlightBackground();
        wxColour hoveredBg = is_dark ? wxColour(33, 38, 45) : wxColour(235, 228, 218);

        for (int i = firstVisible; i <= lastVisible; ++i)
        {
            const SearchResultRow &row = m_rows[i];
            int y = i * rowH - m_scroll_offset;
            wxRect rowRect(0, y, clientSize.x, rowH);

            // Selection/hover background
            if (i == m_selected)
            {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(selectedBg));
                dc.DrawRectangle(rowRect);
            }
            else if (i == m_hovered)
            {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(hoveredBg));
                dc.DrawRectangle(rowRect);
            }

            // Icon
            if (m_icons && row.icon_index >= 0 && row.icon_index < 6)
            {
                const ScalableBitmap &icon = m_icons[row.icon_index];
                if (icon.bmp().IsOk())
                {
                    wxBitmap bmp = icon.bmp().GetBitmapFor(this);
                    int iconY = y + (rowH - bmp.GetHeight()) / 2;
                    int iconX = leftPadding + (iconAreaWidth - bmp.GetWidth()) / 2;
                    dc.DrawBitmap(bmp, iconX, iconY, true);
                }
            }

            // Text with highlight segments
            int textX = leftPadding + iconAreaWidth + textLeftMargin;
            int textY = y + (rowH - dc.GetCharHeight()) / 2;

            if (row.highlight_ranges.empty())
            {
                dc.SetTextForeground(normalTextColor);
                dc.DrawText(row.display_text, textX, textY);
            }
            else
            {
                // Build highlight flags
                size_t textLen = row.display_text.length();
                std::vector<bool> highlighted(textLen, false);
                for (const auto &range : row.highlight_ranges)
                {
                    for (size_t j = range.first; j < range.first + range.second && j < textLen; ++j)
                        highlighted[j] = true;
                }

                int curX = textX;
                size_t pos = 0;
                while (pos < textLen)
                {
                    bool isHighlighted = highlighted[pos];
                    size_t runStart = pos;
                    while (pos < textLen && highlighted[pos] == isHighlighted)
                        ++pos;

                    wxString segment = row.display_text.Mid(runStart, pos - runStart);
                    dc.SetTextForeground(isHighlighted ? highlightTextColor : normalTextColor);
                    dc.DrawText(segment, curX, textY);
                    curX += dc.GetTextExtent(segment).x;
                }
            }
        }
    }

    void OnSize(wxSizeEvent &event)
    {
        UpdateScrollbar();
        event.Skip();
    }

    void OnMouseWheel(wxMouseEvent &event)
    {
        const int totalHeight = static_cast<int>(m_rows.size()) * RowHeight();
        const int visibleHeight = GetClientSize().y;
        if (totalHeight <= visibleHeight)
            return;

        int rotation = event.GetWheelRotation();
        int delta = event.GetWheelDelta();
        if (delta == 0)
            return;
        int scrollAmount = (rotation / delta) * RowHeight() * 3;

        int maxScroll = std::max(0, totalHeight - visibleHeight);
        m_scroll_offset = std::max(0, std::min(m_scroll_offset - scrollAmount, maxScroll));

        if (m_scrollbar)
            m_scrollbar->SetThumbPosition(m_scroll_offset);
        Refresh();
    }

    void OnScroll(wxScrollEvent &event)
    {
        m_scroll_offset = event.GetPosition();
        Refresh();
    }

    void OnMotion(wxMouseEvent &event)
    {
        int newHover = HitTest(event.GetPosition());
        if (newHover != m_hovered)
        {
            m_hovered = newHover;
            Refresh();
        }
    }

    void OnLeftDown(wxMouseEvent &event)
    {
        int clicked = HitTest(event.GetPosition());
        if (clicked >= 0)
        {
            m_selected = clicked;
            Refresh();

            // Notify SearchDialog via wxEVT_LISTBOX
            wxCommandEvent selEvent(wxEVT_LISTBOX, GetId());
            selEvent.SetInt(clicked);
            selEvent.SetEventObject(this);
            ProcessWindowEvent(selEvent);
        }
    }

    void OnLeaveWindow(wxMouseEvent &event)
    {
        if (m_hovered != -1)
        {
            m_hovered = -1;
            Refresh();
        }
    }

    // Data
    std::vector<SearchResultRow> m_rows;
    ScalableBitmap *m_icons{nullptr};

    // State
    int m_selected{-1};
    int m_hovered{-1};
    int m_scroll_offset{0};

    ScrollBar *m_scrollbar{nullptr};
};

//------------------------------------------
//          SearchDialog
//------------------------------------------

SearchDialog::SearchDialog(OptionsSearcher *searcher, wxWindow *parent)
    : GUI::DPIDialog(parent ? parent : GUI::wxGetApp().tab_panel(), wxID_ANY, _L("Search"), wxDefaultPosition,
                     wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , searcher(searcher)
{
    SetFont(GUI::wxGetApp().normal_font());
#if _WIN32
    GUI::wxGetApp().UpdateDarkUI(this);
#elif __WXGTK__
    SetBackgroundColour(GUI::wxGetApp().get_window_default_clr());
#endif

    int em = em_unit();
    int border = em;

    // Initialize category icons
    int icon_id = 0;
    for (const std::string &icon : {"cog", "printer", "sla_printer", "spool", "resin", "notification_preferences"})
        m_icons[icon_id++] = ScalableBitmap(this, icon);

    // preFlight: filter input field within the dialog
    m_filter_input = new ::TextInput(this, wxEmptyString, "", "search", wxDefaultPosition, wxSize(em * 50, -1),
                                     wxTE_PROCESS_ENTER);
    m_filter_input->SetFont(GUI::wxGetApp().normal_font());
    GUI::wxGetApp().UpdateDarkUI(m_filter_input);

    m_filter_input->Bind(wxEVT_TEXT,
                         [this](wxCommandEvent &)
                         {
                             wxString val = m_filter_input->GetValue();
                             this->searcher->search(into_u8(val));
                             this->update_list();
                         });

    // Forward key events from the filter input to navigate the results list
    if (wxTextCtrl *ctrl = m_filter_input->GetTextCtrl())
    {
        ctrl->Bind(wxEVT_KEY_DOWN,
                   [this](wxKeyEvent &e)
                   {
                       int key = e.GetKeyCode();
                       if (key == WXK_UP || key == WXK_DOWN || key == WXK_RETURN || key == WXK_NUMPAD_ENTER)
                       {
                           OnKeyDown(e);
                       }
                       else if (key == WXK_ESCAPE)
                       {
                           this->Hide();
                       }
                       else
                       {
                           e.Skip();
                       }
                   });
    }

    // preFlight: custom ScrollBar + owner-drawn results panel for warm-themed scrollbar
    m_scrollbar = new ScrollBar(this);
    m_results_panel = new SearchResultsPanel(this, m_scrollbar);
    m_results_panel->SetMinSize(wxSize(em * 50, em * 30));
    m_results_panel->SetFont(GUI::wxGetApp().normal_font());
    m_results_panel->SetBackgroundColour(UIColors::InputBackground());

    // Handle click-to-activate from the results panel
    m_results_panel->Bind(wxEVT_LISTBOX, &SearchDialog::OnResultClicked, this);

    wxBoxSizer *check_sizer = new wxBoxSizer(wxHORIZONTAL);

    check_category = new ::CheckBox(this, _L("Category"));
    if (GUI::wxGetApp().is_localized())
        check_english = new ::CheckBox(this, _L("Search in English"));

    wxStdDialogButtonSizer *cancel_btn = this->CreateStdDialogButtonSizer(wxCANCEL);
    GUI::wxGetApp().UpdateDarkUI(static_cast<wxButton *>(this->FindWindowById(wxID_CANCEL, this)));

    check_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Use for search") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                     border);
    check_sizer->Add(check_category, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    if (check_english)
        check_sizer->Add(check_english, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->AddStretchSpacer(border);
    check_sizer->Add(cancel_btn, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);

    // preFlight: filter input at top
    topSizer->Add(m_filter_input, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);

    // preFlight: results panel + custom scrollbar side by side
    wxBoxSizer *list_row = new wxBoxSizer(wxHORIZONTAL);
    list_row->Add(m_results_panel, 1, wxEXPAND);
    list_row->Add(m_scrollbar, 0, wxEXPAND);

    topSizer->Add(list_row, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(check_sizer, 0, wxEXPAND | wxALL, border);

    check_category->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);
    if (check_english)
        check_english->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    // preFlight: center on parent so the dialog never appears off-screen
    CenterOnParent();

#ifdef _WIN32
    GUI::wxGetApp().UpdateDlgDarkUI(this);
#endif
}

SearchDialog::~SearchDialog() {}

void SearchDialog::Popup(wxPoint position /*= wxDefaultPosition*/)
{
    // Sync the filter input with current search string
    if (m_filter_input)
    {
        wxString current = from_u8(searcher->search_string());
        if (m_filter_input->GetValue() != current)
            m_filter_input->SetValue(current);
    }

    update_list();

    const OptionViewParameters &params = searcher->view_params;
    check_category->SetValue(params.category);
    if (check_english)
        check_english->SetValue(params.english);

    // preFlight: center on the main application window each time
    if (wxWindow *top = GUI::wxGetApp().GetTopWindow())
    {
        wxRect frame_rect = top->GetScreenRect();
        wxSize dlg_size = GetSize();
        int x = frame_rect.x + (frame_rect.width - dlg_size.x) / 2;
        int y = frame_rect.y + (frame_rect.height - dlg_size.y) / 2;
        SetPosition(wxPoint(x, y));
    }
    this->Show();

    // Focus the filter input so the user can type immediately
    if (m_filter_input)
    {
        m_filter_input->SetFocus();
        if (wxTextCtrl *ctrl = m_filter_input->GetTextCtrl())
        {
            ctrl->SetFocus();
            ctrl->SelectAll();
        }
    }
}

void SearchDialog::ProcessSelection(int row_index)
{
    if (row_index < 0 || row_index >= m_results_panel->GetItemCount())
        return;

    this->Hide();

    // Post event to mainframe so the dialog loses focus first,
    // then jump_to_option() can properly activate the found option.
    wxCommandEvent event(wxCUSTOMEVT_JUMP_TO_OPTION);
    event.SetInt(row_index);
    wxPostEvent(GUI::wxGetApp().mainframe, event);
}

void SearchDialog::OnResultClicked(wxCommandEvent &event)
{
    ProcessSelection(event.GetInt());
}

void SearchDialog::input_text(wxString input_string)
{
    if (input_string == searcher->default_string)
        input_string.Clear();

    // Sync the filter input if it differs (avoid re-triggering wxEVT_TEXT loop)
    if (m_filter_input && m_filter_input->GetValue() != input_string)
        m_filter_input->SetValue(input_string);

    searcher->search(into_u8(input_string));

    update_list();
}

void SearchDialog::OnKeyDown(wxKeyEvent &event)
{
    int key = event.GetKeyCode();

    if (key == WXK_UP)
    {
        m_results_panel->SelectPrev();
    }
    else if (key == WXK_DOWN)
    {
        m_results_panel->SelectNext();
    }
    else if (key == WXK_NUMPAD_ENTER || key == WXK_RETURN)
    {
        ProcessSelection(m_results_panel->GetSelection());
    }
    else
    {
        event.Skip();
    }
}

void SearchDialog::update_list()
{
    // Use marked_label (with ColorMarkerStart/End) for proper accent-color match highlighting
    m_results_panel->SetItems(searcher->found_options(), m_icons);
}

void SearchDialog::OnCheck(wxCommandEvent &event)
{
    OptionViewParameters &params = searcher->view_params;
    if (check_english)
        params.english = check_english->GetValue();
    params.category = check_category->GetValue();

    searcher->search();
    update_list();
}

void SearchDialog::msw_rescale()
{
    const int &em = em_unit();
    const wxSize &size = wxSize(40 * em, 30 * em);
    SetMinSize(size);

    // Re-create icons at new DPI
    int icon_id = 0;
    for (const std::string &icon : {"cog", "printer", "sla_printer", "spool", "resin", "notification_preferences"})
        m_icons[icon_id++] = ScalableBitmap(this, icon);

    if (m_filter_input)
        m_filter_input->Rescale();

    if (m_scrollbar)
        m_scrollbar->msw_rescale();

    if (m_results_panel)
        m_results_panel->msw_rescale();

    Fit();
    Refresh();
}

void SearchDialog::on_sys_color_changed()
{
#ifdef _WIN32
    GUI::wxGetApp().UpdateAllStaticTextDarkUI(this);
    GUI::wxGetApp().UpdateDarkUI(static_cast<wxButton *>(this->FindWindowById(wxID_CANCEL, this)), true);
    for (wxWindow *win : std::vector<wxWindow *>{check_category, check_english})
        if (win)
            GUI::wxGetApp().UpdateDarkUI(win);
#endif

    // Update icons for new theme
    for (ScalableBitmap &bmp : m_icons)
        bmp.sys_color_changed();

    if (m_filter_input)
        m_filter_input->SysColorsChanged();

    if (m_scrollbar)
        m_scrollbar->sys_color_changed();

    if (m_results_panel)
        m_results_panel->sys_color_changed();

    Refresh();
}

} // namespace Search

} // namespace Slic3r
