///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// A project archive carries up to four auxiliary XML parts named Metadata/<prefix>_<suffix>. Writers of the same
// schema use their own prefix, so a part is recognized by its suffix under a non-empty prefix, and the own prefix
// decides which one is read when an archive holds more than one of a kind.
namespace Luminary
{

enum class AuxPartKind
{
    None,
    LayerConfigRanges,
    CustomGCodePerPrintZ,
    WipeTowerInformation,
    CutInformation
};

struct AuxPartName
{
    AuxPartKind kind = AuxPartKind::None;
    std::string prefix;
    bool own = false;
};

namespace aux_part_detail
{

inline char ascii_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

inline bool ascii_iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(a[i]) != ascii_lower(b[i]))
            return false;
    return true;
}

} // namespace aux_part_detail

// Classifies an archive entry name ('/' separators) as one of the four auxiliary parts: Metadata/<prefix>_<suffix>
// directly under Metadata/, with a non-empty prefix, compared ASCII case-insensitively. A name without a prefix is
// not matched, because other writers use those names for parts with different ids and keys.
inline AuxPartName classify_aux_part(std::string_view name)
{
    using aux_part_detail::ascii_iequals;

    struct Suffix
    {
        std::string_view text;
        AuxPartKind kind;
    };
    static constexpr std::string_view folder = "Metadata/";
    static constexpr std::string_view own_prefix = "preFlight";
    // Each suffix carries the '_' that ends the prefix
    static constexpr Suffix suffixes[] = {
        {"_layer_config_ranges.xml", AuxPartKind::LayerConfigRanges},
        {"_custom_gcode_per_print_z.xml", AuxPartKind::CustomGCodePerPrintZ},
        {"_wipe_tower_information.xml", AuxPartKind::WipeTowerInformation},
        {"_cut_information.xml", AuxPartKind::CutInformation},
    };

    AuxPartName result;
    if (name.size() < folder.size() || !ascii_iequals(name.substr(0, folder.size()), folder))
        return result;
    const std::string_view file = name.substr(folder.size());
    if (file.find('/') != std::string_view::npos)
        return result;
    for (const Suffix &suffix : suffixes)
    {
        // Strictly longer than the suffix, so the prefix is not empty
        if (file.size() <= suffix.text.size())
            continue;
        const size_t prefix_size = file.size() - suffix.text.size();
        if (!ascii_iequals(file.substr(prefix_size), suffix.text))
            continue;
        result.kind = suffix.kind;
        result.prefix = std::string(file.substr(0, prefix_size));
        result.own = ascii_iequals(result.prefix, own_prefix);
        return result;
    }
    return result;
}

struct AuxPartSelection
{
    std::array<int, 4> chosen{-1, -1, -1, -1}; // archive index per kind (index = int(kind) - 1), -1 when absent
    std::array<bool, 4> foreign{};             // the chosen part has another prefix than preFlight
    unsigned duplicates = 0;                   // matching parts left unread
};

// Picks the part to read for each kind from the archive's entry names (index = archive index): the first one with
// the own prefix if there is one, otherwise the first one with another prefix. Every other part of that kind is
// counted as a duplicate. An empty name (an entry the caller could not stat) is ignored.
inline AuxPartSelection select_aux_parts(const std::vector<std::string> &names)
{
    AuxPartSelection selection;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i].empty())
            continue;
        const AuxPartName part = classify_aux_part(names[i]);
        if (part.kind == AuxPartKind::None)
            continue;
        const size_t k = size_t(part.kind) - 1;
        if (selection.chosen[k] < 0)
        {
            selection.chosen[k] = int(i);
            selection.foreign[k] = !part.own;
            continue;
        }
        // An own part replaces a foreign one chosen earlier; either way one part of this kind stays unread
        if (part.own && selection.foreign[k])
        {
            selection.chosen[k] = int(i);
            selection.foreign[k] = false;
        }
        ++selection.duplicates;
    }
    return selection;
}

} // namespace Luminary
