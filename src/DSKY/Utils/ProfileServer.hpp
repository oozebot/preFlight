///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <map>
#include <string>
#include <boost/filesystem/path.hpp>

namespace Luminary
{

// Simple client for fetching vendor profiles from profiles.preflight3d.com.
// All methods are synchronous and safe to call from any thread.
class ProfileServer
{
public:
    static constexpr const char *BASE_URL = "https://profiles.preflight3d.com";

    // Vendor entry from index.json: vendor_id -> version string
    using VendorIndex = std::map<std::string, std::string>;

    // Downloads and parses index.json. Returns empty map on failure.
    static VendorIndex fetch_index();

    // Downloads a file from BASE_URL/subpath to target_path.
    // Uses tmp file + atomic rename. Returns true on success.
    static bool fetch_file(const std::string &subpath, const boost::filesystem::path &target_path);

    // Downloads {vendor_id}.ini to data_dir()/vendor/{vendor_id}.ini
    static bool fetch_vendor_ini(const std::string &vendor_id);

    // Downloads {vendor_id}/{filename} to data_dir()/vendor/{vendor_id}/{filename}
    static bool fetch_resource(const std::string &vendor_id, const std::string &filename);

    // Returns data_dir()/vendor/{vendor_id}.ini
    static boost::filesystem::path vendor_ini_path(const std::string &vendor_id);

    // Returns data_dir()/vendor/{vendor_id}/{filename}
    static boost::filesystem::path vendor_resource_path(const std::string &vendor_id, const std::string &filename);

    // Returns true if the vendor .ini is already cached locally
    static bool is_vendor_cached(const std::string &vendor_id);

    // Downloads Filaments.ini to data_dir()/vendor/Filaments.ini
    static bool fetch_filaments_bundle();
};

} // namespace Luminary

