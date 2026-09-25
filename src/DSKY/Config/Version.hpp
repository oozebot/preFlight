///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2020 Lukáš Matěna @lukasmatena, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

#include "luminary/core/FileParserError.hpp"
#include "luminary/core/Semver.hpp"

namespace DSKY
{
using namespace Luminary;

namespace Config
{

// Configuration bundle version.
struct Version
{
    // Version of this config.
    Semver config_version = Semver::invalid();
    // Minimum application version for which this config is applicable.
    Semver min_preflight_version = Semver::zero();
    // Maximum application version for which this config is recommended.
    // preFlight reads older configuration and upgrades it to a newer format,
    // but likely there has been a better configuration published, using the new features.
    Semver max_preflight_version = Semver::inf();
    // Single comment line.
    std::string comment;

    bool is_preflight_supported(const Semver &preflight_version) const;
    bool is_current_preflight_supported() const;
    bool is_current_preflight_downgrade() const;
};

// Index of vendor specific config bundle versions and their application compatibilities.
// The index is being downloaded from the internet, also an initial version of the index
// is contained in the preFlight installation.
//
// The index has a simple format:
//
// min_preflight_version =
// max_preflight_version =
// config_version "comment"
// config_version "comment"
// ...
// min_preflight_version =
// max_preflight_version =
// config_version comment
// config_version comment
// ...
//
// The min_preflight_version, max_preflight_version keys are applied to the config versions below,
// an empty version means an open interval. An index written under the earlier key spellings is
// read too, and that read is counted.
class Index
{
public:
    typedef std::vector<Version>::const_iterator const_iterator;
    // Read a config index file in the simple format described in the Index class comment.
    // Throws Luminary::file_parser_error and the standard std file access exceptions.
    size_t load(const boost::filesystem::path &path);

    const std::string &vendor() const { return m_vendor; }
    // Returns version of the index as the highest version of all the configs.
    // If there is no config, Semver::zero() is returned.
    Semver version() const;

    const_iterator begin() const { return m_configs.begin(); }
    const_iterator end() const { return m_configs.end(); }
    const_iterator find(const Semver &ver) const;
    const std::vector<Version> &configs() const { return m_configs; }
    // Finds a recommended config to be installed for the current application version.
    // Returns configs().end() if such version does not exist in the index. This shall never happen
    // if the index is valid.
    const_iterator recommended() const;
    // Recommended config for a provided application version. Used when checking for an update
    // (preflight_version is the old one read out from preFlight.ini).
    const_iterator recommended(const Semver &preflight_version) const;

    // Returns the filesystem path from which this index has originally been loaded
    const boost::filesystem::path &path() const { return m_path; }

    // Load all vendor specific indices.
    // Throws Luminary::file_parser_error and the standard std file access exceptions.
    static std::vector<Index> load_db();

private:
    std::string m_vendor;
    std::vector<Version> m_configs;
    boost::filesystem::path m_path;
};

} // namespace Config
} // namespace DSKY

