///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2022 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, David Kocík @kocikdav, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "Version.hpp"

#include <cctype>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/nowide/fstream.hpp>

#include "luminary/core/Prelude.hpp"
#include "luminary/config/model/Config.hpp"
#include "luminary/core/FileParserError.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/platform/files/FileIO.hpp"
#include "luminary/platform/paths/Paths.hpp"

namespace DSKY
{
using namespace Luminary;

namespace Config
{

// Optimized lexicographic compare of two pre-release versions, ignoring the numeric suffix.
static int compare_prerelease(const char *p1, const char *p2)
{
    for (;;)
    {
        char c1 = *p1++;
        char c2 = *p2++;
        bool a1 = std::isalpha(c1) && c1 != 0;
        bool a2 = std::isalpha(c2) && c2 != 0;
        if (a1)
        {
            if (a2)
            {
                if (c1 != c2)
                    return (c1 < c2) ? -1 : 1;
            }
            else
                return 1;
        }
        else
        {
            if (a2)
                return -1;
            else
                return 0;
        }
    }
    // This shall never happen.
    return 0;
}

bool Version::is_preflight_supported(const Semver &preflight_version) const
{
    if (!preflight_version.in_range(min_preflight_version, max_preflight_version))
        return false;
    // Now verify whether the configuration pre-release status is compatible with the application's pre-release status.
    // An alpha build loads any configuration, while a beta build ignores alpha configurations.
    const char *prerelease_app = preflight_version.prerelease();
    const char *prerelease_config = this->config_version.prerelease();
    if (prerelease_config == nullptr)
        // Released config is always supported.
        return true;
    else if (prerelease_app == nullptr)
        // A released build only supports released configs.
        return false;
    // Compare the pre-release status of the application against the config.
    // If the prerelease status of the application is lexicographically lower or equal
    // to the prerelease status of the config, accept it.
    return compare_prerelease(prerelease_app, prerelease_config) != 1;
}

bool Version::is_current_preflight_supported() const
{
    return this->is_preflight_supported(Luminary::SEMVER);
}

bool Version::is_current_preflight_downgrade() const
{
    return Luminary::SEMVER < min_preflight_version;
}

inline char *left_trim(char *c)
{
    for (; *c == ' ' || *c == '\t'; ++c)
        ;
    return c;
}

inline char *right_trim(char *start)
{
    char *end = start + strlen(start) - 1;
    for (; end >= start && (*end == ' ' || *end == '\t'); --end)
        ;
    *(++end) = 0;
    return end;
}

inline std::string unquote_value(char *value, char *end, const std::string &path, int idx_line)
{
    std::string svalue;
    if (value == end)
    {
        // Empty string is a valid string.
    }
    else if (*value == '"')
    {
        if (++value > --end || *end != '"')
            throw file_parser_error("String not enquoted correctly", path, idx_line);
        *end = 0;
        if (!unescape_string_cstyle(value, svalue))
            throw file_parser_error("Invalid escape sequence inside a quoted string", path, idx_line);
    }
    else
        svalue.assign(value, end);
    return svalue;
}

inline std::string unquote_version_comment(char *value, char *end, const std::string &path, int idx_line)
{
    std::string svalue;
    if (value == end)
    {
        // Empty string is a valid string.
    }
    else if (*value == '"')
    {
        if (++value > --end || *end != '"')
            throw file_parser_error("Version comment not enquoted correctly", path, idx_line);
        *end = 0;
        if (!unescape_string_cstyle(value, svalue))
            throw file_parser_error("Invalid escape sequence inside a quoted version comment", path, idx_line);
    }
    else
        svalue.assign(value, end);
    return svalue;
}

size_t Index::load(const boost::filesystem::path &path)
{
    m_configs.clear();
    m_vendor = path.stem().string();
    m_path = path;

    boost::nowide::ifstream ifs(path.string());
    std::string line;
    size_t idx_line = 0;
    Version ver;
    while (std::getline(ifs, line))
    {
#ifndef _MSVCVER
        // On a Unix system, getline does not remove the trailing carriage returns, if the index is shared over a Windows filesystem. Remove them manually.
        while (!line.empty() && line.back() == '\r')
            line.pop_back();
#endif
        ++idx_line;
        // Skip the initial white spaces.
        char *key = left_trim(line.data());
        if (*key == '#')
            // Skip a comment line.
            continue;
        // Right trim the line.
        char *end = right_trim(key);
        if (key == end)
            // Skip an empty line.
            continue;
        // Keyword may only contain alphanumeric characters. Semantic version may in addition contain "+.-".
        char *key_end = key;
        bool maybe_semver = true;
        for (; *key_end != 0; ++key_end)
        {
            if (std::isalnum(*key_end) || strchr("+.-", *key_end) != nullptr)
            {
                // It may be a semver.
            }
            else if (*key_end == '_')
            {
                // Cannot be a semver, but it may be a key.
                maybe_semver = false;
            }
            else
                // End of semver or keyword.
                break;
        }
        if (*key_end != 0 && *key_end != ' ' && *key_end != '\t' && *key_end != '=')
            throw file_parser_error("Invalid keyword or semantic version", path, idx_line);
        char *value = left_trim(key_end);
        bool key_value_pair = *value == '=';
        if (key_value_pair)
            value = left_trim(value + 1);
        *key_end = 0;
        boost::optional<Semver> semver;
        if (maybe_semver)
            semver = Semver::parse(key);
        if (key_value_pair)
        {
            if (semver)
                throw file_parser_error("Key cannot be a semantic version", path,
                                        idx_line); // Verify validity of the key / value pair.
            std::string svalue = unquote_value(value, end, path.string(), idx_line);
            const bool legacy_version_key = strcmp(key, "min_slic3r_version") == 0 ||
                                            strcmp(key, "max_slic3r_version") == 0;
            if (strcmp(key, "min_preflight_version") == 0 || strcmp(key, "max_preflight_version") == 0 ||
                legacy_version_key)
            {
                // An index written under the earlier key spellings keeps loading; that read is counted.
                if (legacy_version_key)
                    DBG_COUNT_LOAD("CONFIG_LEGACY_VERSION_KEY");
                if (!svalue.empty())
                    semver = Semver::parse(svalue);
                if (!semver)
                    throw file_parser_error(std::string(key) + " must referece a valid semantic version", path,
                                            idx_line);
                if (strncmp(key, "min_", 4) == 0)
                    ver.min_preflight_version = *semver;
                else
                    ver.max_preflight_version = *semver;
            }
            else
            {
                // Ignore unknown keys, as there may come new keys in the future.
            }
            continue;
        }
        if (!semver)
            throw file_parser_error("Invalid semantic version", path, idx_line);
        ver.config_version = *semver;
        ver.comment = (end <= key_end) ? "" : unquote_version_comment(value, end, path.string(), idx_line);
        m_configs.emplace_back(ver);
    }

    // Sort the configs by their version.
    std::sort(m_configs.begin(), m_configs.end(),
              [](const Version &v1, const Version &v2) { return v1.config_version < v2.config_version; });
    return m_configs.size();
}

Semver Index::version() const
{
    Semver ver = Semver::zero();
    for (const Version &cv : m_configs)
        if (cv.config_version >= ver)
            ver = cv.config_version;
    return ver;
}

Index::const_iterator Index::find(const Semver &ver) const
{
    Version key;
    key.config_version = ver;
    auto it = std::lower_bound(m_configs.begin(), m_configs.end(), key, [](const Version &v1, const Version &v2)
                               { return v1.config_version < v2.config_version; });
    return (it == m_configs.end() || it->config_version == ver) ? it : m_configs.end();
}

Index::const_iterator Index::recommended(const Semver &preflight_version) const
{
    const_iterator highest = this->end();
    for (const_iterator it = this->begin(); it != this->end(); ++it)
        if (it->is_preflight_supported(preflight_version) &&
            (highest == this->end() || highest->config_version < it->config_version))
            highest = it;
    return highest;
}

Index::const_iterator Index::recommended() const
{
    return this->recommended(Luminary::SEMVER);
}

std::vector<Index> Index::load_db()
{
    boost::filesystem::path cache_dir = boost::filesystem::path(Luminary::data_dir()) / "cache";
    boost::filesystem::path vendor_dir = boost::filesystem::path(Luminary::data_dir()) / "vendor";

    std::vector<Index> index_db;
    std::string errors_cummulative;

    for (auto &dir_entry : boost::filesystem::directory_iterator(cache_dir))
        if (Luminary::is_idx_file(dir_entry))
        {
            Index idx;
            try
            {
                idx.load(dir_entry.path());
            }
            catch (const std::runtime_error &err)
            {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
                continue;
            }
            index_db.emplace_back(std::move(idx));
        }

    for (auto &dir_entry : boost::filesystem::directory_iterator(vendor_dir))
        if (Luminary::is_idx_file(dir_entry))
        {
            Index idx;
            try
            {
                idx.load(dir_entry.path());
            }
            catch (const std::runtime_error &err)
            {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
                continue;
            }
            if (std::find_if(index_db.begin(), index_db.end(),
                             [idx](const Index &index) { return idx.vendor() == index.vendor(); }) == index_db.end())
                index_db.emplace_back(std::move(idx));
        }

    if (!errors_cummulative.empty())
        throw Luminary::RuntimeError(errors_cummulative);
    return index_db;
}

} // namespace Config
} // namespace DSKY
