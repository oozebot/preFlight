///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "ProfileServer.hpp"
#include "Http.hpp"
#include "luminary/core/Prelude.hpp"
#include "luminary/platform/paths/Paths.hpp"

#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace fs = boost::filesystem;

namespace Luminary
{

ProfileServer::VendorIndex ProfileServer::fetch_index()
{
    VendorIndex index;
    std::string url = std::string(BASE_URL) + "/index.json";

    BOOST_LOG_TRIVIAL(info) << "ProfileServer: fetching " << url;

    Http::get(url)
        .timeout_max(15)
        .on_error(
            [&](std::string body, std::string error, unsigned http_status)
            {
                BOOST_LOG_TRIVIAL(error) << "ProfileServer: failed to fetch index.json: HTTP " << http_status << " "
                                         << error;
            })
        .on_complete(
            [&](std::string body, unsigned http_status)
            {
                if (http_status < 200 || http_status >= 300 || body.empty())
                    return;
                try
                {
                    std::istringstream ss(body);
                    boost::property_tree::ptree pt;
                    boost::property_tree::read_json(ss, pt);
                    for (const auto &[key, val] : pt)
                        index[key] = val.get_value<std::string>();
                }
                catch (const std::exception &e)
                {
                    BOOST_LOG_TRIVIAL(error) << "ProfileServer: failed to parse index.json: " << e.what();
                    index.clear();
                }
            })
        .perform_sync();

    BOOST_LOG_TRIVIAL(info) << "ProfileServer: index contains " << index.size() << " vendors";
    return index;
}

bool ProfileServer::fetch_file(const std::string &subpath, const fs::path &target_path)
{
    // URL-encode each path segment individually to preserve slashes
    std::string encoded_subpath;
    std::istringstream segments(subpath);
    std::string segment;
    while (std::getline(segments, segment, '/'))
    {
        if (!encoded_subpath.empty())
            encoded_subpath += '/';
        encoded_subpath += Http::url_encode(segment);
    }
    std::string url = std::string(BASE_URL) + "/" + encoded_subpath;
    bool success = false;

    fs::create_directories(target_path.parent_path());

    // Atomic write via tmp file
    fs::path tmp_path = target_path;
    tmp_path += ".tmp";

    BOOST_LOG_TRIVIAL(info) << "ProfileServer: downloading " << url;

    Http::get(url)
        .timeout_max(30)
        .on_error(
            [&](std::string body, std::string error, unsigned http_status)
            {
                BOOST_LOG_TRIVIAL(error) << "ProfileServer: download failed: " << url << " HTTP " << http_status << " "
                                         << error;
            })
        .on_complete(
            [&](std::string body, unsigned http_status)
            {
                if (http_status < 200 || http_status >= 300)
                {
                    BOOST_LOG_TRIVIAL(error) << "ProfileServer: HTTP " << http_status << " for " << url;
                    return;
                }
                if (body.empty())
                    return;
                boost::nowide::ofstream file(tmp_path.string(), std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                {
                    BOOST_LOG_TRIVIAL(error) << "ProfileServer: cannot write to " << tmp_path.string();
                    return;
                }
                file.write(body.c_str(), body.size());
                file.close();

                boost::system::error_code ec;
                fs::rename(tmp_path, target_path, ec);
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(error) << "ProfileServer: rename failed: " << ec.message();
                    fs::remove(tmp_path, ec);
                    return;
                }
                success = true;
            })
        .perform_sync();

    return success;
}

// Reject path components that could escape the vendor directory
static bool is_safe_path_component(const std::string &s)
{
    if (s.empty())
        return false;
    if (s.find("..") != std::string::npos)
        return false;
    if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos)
        return false;
    if (s.find('\0') != std::string::npos)
        return false;
    return true;
}

bool ProfileServer::fetch_vendor_ini(const std::string &vendor_id)
{
    if (!is_safe_path_component(vendor_id))
    {
        BOOST_LOG_TRIVIAL(error) << "ProfileServer: rejected unsafe vendor_id: " << vendor_id;
        return false;
    }
    return fetch_file(vendor_id + ".ini", vendor_ini_path(vendor_id));
}

bool ProfileServer::fetch_resource(const std::string &vendor_id, const std::string &filename)
{
    if (!is_safe_path_component(vendor_id) || !is_safe_path_component(filename))
    {
        BOOST_LOG_TRIVIAL(error) << "ProfileServer: rejected unsafe path component: " << vendor_id << "/" << filename;
        return false;
    }
    return fetch_file(vendor_id + "/" + filename, vendor_resource_path(vendor_id, filename));
}

fs::path ProfileServer::vendor_ini_path(const std::string &vendor_id)
{
    return fs::path(data_dir()) / "vendor" / (vendor_id + ".ini");
}

fs::path ProfileServer::vendor_resource_path(const std::string &vendor_id, const std::string &filename)
{
    return fs::path(data_dir()) / "vendor" / vendor_id / filename;
}

bool ProfileServer::is_vendor_cached(const std::string &vendor_id)
{
    if (!is_safe_path_component(vendor_id))
        return false;
    return fs::exists(vendor_ini_path(vendor_id));
}

bool ProfileServer::fetch_filaments_bundle()
{
    return fetch_file("Filaments.ini", fs::path(data_dir()) / "vendor" / "Filaments.ini");
}

} // namespace Luminary
