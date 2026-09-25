///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena
///|/ Copyright (c) 2022 KARBOWSKI Piotr
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "AppUpdater.hpp"

#include <atomic>
#include <thread>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <curl/curl.h>

#include "DSKY/GUI/format.hpp"
#include "DSKY/GUI/GUI_App.hpp"
#include "DSKY/GUI/GUI.hpp"
#include "DSKY/GUI/I18N.hpp"
#include "DSKY/GUI/GLCanvas3D.hpp"
#include "DSKY/Utils/Http.hpp"

#include "luminary/platform/paths/Paths.hpp"

#ifdef __linux__
#include <sys/utsname.h>
#endif

#include <boost/algorithm/string.hpp>

#ifdef _WIN32
#include <shellapi.h>
#include <Shlobj_core.h>
#include <windows.h>
#include <KnownFolders.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#endif // _WIN32

namespace Luminary
{

namespace
{

// A downloaded installer is started only when it carries oozeBot's code signature; anything
// else is left in the download folder with a warning, for the user to inspect.
void refuse_unsigned_download(const boost::filesystem::path &path, const std::string &signer)
{
    std::string full_message = DSKY::format(
        _u8L("The downloaded %1% installer is not signed by oozeBot (%2%). It was not started; it is in %3% "
             "for you to check before running it."),
        PREFLIGHT_APP_NAME, signer.empty() ? std::string("unsigned") : signer, path.parent_path().string());
    BOOST_LOG_TRIVIAL(error) << full_message;
    wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED);
    evt->SetString(full_message);
    if (wxApp::GetInstance() != nullptr)
        DSKY::wxGetApp().QueueEvent(evt);
    DSKY::desktop_open_folder(path.parent_path());
}

#ifdef _WIN32
// Authenticode verification plus the signer's subject: only a certificate whose subject names
// oozeBot counts as ours.
bool signed_by_oozebot(const boost::filesystem::path &path, std::string &signer)
{
    const std::wstring wpath = path.wstring();
    WINTRUST_FILE_INFO file_info{};
    file_info.cbStruct = sizeof(file_info);
    file_info.pcwszFilePath = wpath.c_str();
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file_info;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_SAFER_FLAG;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(nullptr, &action, &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trust);
    if (status != ERROR_SUCCESS)
        return false;

    HCERTSTORE store = nullptr;
    HCRYPTMSG message = nullptr;
    DWORD encoding = 0, content_type = 0, format_type = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, wpath.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &content_type, &format_type, &store, &message,
                          nullptr))
        return false;
    bool ours = false;
    DWORD size = 0;
    if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &size) && size > 0)
    {
        std::vector<BYTE> buffer(size);
        auto *signer_info = reinterpret_cast<CMSG_SIGNER_INFO *>(buffer.data());
        if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, signer_info, &size))
        {
            CERT_INFO cert_info{};
            cert_info.Issuer = signer_info->Issuer;
            cert_info.SerialNumber = signer_info->SerialNumber;
            if (PCCERT_CONTEXT cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                                 CERT_FIND_SUBJECT_CERT, &cert_info, nullptr))
            {
                const DWORD length = CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
                std::wstring name(length, L'\0');
                CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name.data(), length);
                signer = boost::nowide::narrow(name.c_str());
                ours = boost::algorithm::icontains(signer, "oozeBot");
                CertFreeCertificateContext(cert);
            }
        }
    }
    CryptMsgClose(message);
    CertCloseStore(store, 0);
    return ours;
}

bool run_file(const boost::filesystem::path &path)
{
    std::string signer;
    if (!signed_by_oozebot(path, signer))
    {
        refuse_unsigned_download(path, signer);
        return false;
    }
    std::string msg;
    bool res = DSKY::create_process(path, std::wstring(), msg);
    if (!res)
    {
        std::string full_message = DSKY::format(_u8L("Running downloaded instaler of %1% has failed:\n%2%"),
                                                PREFLIGHT_APP_NAME, msg);
        BOOST_LOG_TRIVIAL(error) << full_message;
        wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED);
        evt->SetString(full_message);
        if (wxApp::GetInstance() != nullptr)
            DSKY::wxGetApp().QueueEvent(evt);
    }
    return res;
}

std::string get_downloads_path()
{
    std::string ret;
    PWSTR path = NULL;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path);
    if (SUCCEEDED(hr))
    {
        ret = boost::nowide::narrow(path);
    }
    CoTaskMemFree(path);
    return ret;
}
#elif __APPLE__
// The disk image must carry oozeBot's Developer ID signature (team LZKRT9D87D).
bool signed_by_oozebot(const boost::filesystem::path &path, std::string &signer)
{
    const std::string command = "codesign -dvv \"" + path.string() + "\" 2>&1";
    FILE *pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr)
        return false;
    std::string output;
    char buffer[512];
    while (::fgets(buffer, sizeof(buffer), pipe) != nullptr)
        output += buffer;
    ::pclose(pipe);
    const size_t pos = output.find("TeamIdentifier=");
    if (pos != std::string::npos)
        signer = output.substr(pos, output.find('\n', pos) - pos);
    return output.find("TeamIdentifier=LZKRT9D87D") != std::string::npos;
}

bool run_file(const boost::filesystem::path &path)
{
    std::string signer;
    if (!signed_by_oozebot(path, signer))
    {
        refuse_unsigned_download(path, signer);
        return false;
    }
    if (boost::filesystem::exists(path))
    {
        // attach downloaded dmg file
        const char *argv1[] = {"hdiutil", "attach", path.string().c_str(), nullptr};
        ::wxExecute(const_cast<char **>(argv1), wxEXEC_ASYNC, nullptr);
        // open inside attached as a folder in finder
        const char *argv2[] = {"open", "/Volumes/preFlight", nullptr};
        ::wxExecute(const_cast<char **>(argv2), wxEXEC_ASYNC, nullptr);
        return true;
    }
    return false;
}

std::string get_downloads_path()
{
    // call objective-c implementation
    return get_downloads_path_mac();
}
#else
bool run_file(const boost::filesystem::path &path)
{
    return false;
}

std::string get_downloads_path()
{
    wxString command = "xdg-user-dir DOWNLOAD";
    wxArrayString output;
    DSKY::desktop_execute_get_result(command, output);
    if (output.GetCount() > 0)
    {
        return output[0]
            .ToUTF8()
            .data(); //lm:I would use wxString::ToUTF8(), although on Linux, nothing at all should work too.
    }
    return std::string();
}
#endif // _WIN32 / __apple__ / else
} // namespace

wxDEFINE_EVENT(EVT_PREFLIGHT_VERSION_ONLINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_PREFLIGHT_EXPERIMENTAL_VERSION_ONLINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_PREFLIGHT_APP_DOWNLOAD_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED, wxCommandEvent);
wxDEFINE_EVENT(EVT_PREFLIGHT_APP_OPEN_FAILED, wxCommandEvent);

// priv handles all operations in separate thread
// 1) download version file and parse it.
// 2) download new app file and open in folder / run it.
struct AppUpdater::priv
{
    priv();
    // Download file. What happens with the data is specified in completefn.
    bool http_get_file(const std::string &url, size_t size_limit, std::function<bool(Http::Progress)> progress_fn,
                       std::function<bool(std::string /*body*/, std::string &error_message)> completefn,
                       std::string &error_message) const;

    // Download installer / app
    boost::filesystem::path download_file(const DownloadAppData &data) const;
    // Run file in m_last_dest_path
    bool run_downloaded_file(boost::filesystem::path path);
    // gets version file via http
    void version_check(const std::string &version_check_url);
    // parses ini tree of version file, saves to m_online_version_data and queue event(s) to UI
    void parse_version_string(const std::string &body);
    // thread
    std::thread m_thread;
    std::atomic_bool m_cancel;
    std::mutex m_data_mutex;
    // used to tell if notify user hes about to stop ongoing download
    std::atomic_bool m_download_ongoing{false};
    bool get_download_ongoing() const { return m_download_ongoing; }
    // read only variable used to init m_online_version_data.target_path
    boost::filesystem::path m_default_dest_folder; // readonly
    // DownloadAppData read / write needs to be locked by m_data_mutex
    DownloadAppData m_online_version_data;
    DownloadAppData get_app_data();
    void set_app_data(DownloadAppData data);
    // set only before version file is downloaded, to keep information to show info dialog about no updates
    // should never change during thread run
    std::atomic_bool m_triggered_by_user{false};
    bool get_triggered_by_user() const { return m_triggered_by_user; }
};

AppUpdater::priv::priv()
    : m_cancel(false)
#ifdef __linux__
    , m_default_dest_folder(boost::filesystem::path("/tmp"))
#else
    , m_default_dest_folder(boost::filesystem::path(data_dir()) / "cache")
#endif //_WIN32
{
    boost::filesystem::path downloads_path = boost::filesystem::path(get_downloads_path());
    if (!downloads_path.empty())
    {
        m_default_dest_folder = std::move(downloads_path);
    }
    BOOST_LOG_TRIVIAL(trace) << "App updater default download path: " << m_default_dest_folder;
}

bool AppUpdater::priv::http_get_file(const std::string &url, size_t size_limit,
                                     std::function<bool(Http::Progress)> progress_fn,
                                     std::function<bool(std::string /*body*/, std::string &error_message)> complete_fn,
                                     std::string &error_message) const
{
    bool res = false;
    Http::get(url)
        .size_limit(size_limit)
        .on_progress(
            [&, progress_fn](Http::Progress progress, bool &cancel)
            {
                // progress function returns true as success (to continue)
                cancel = (m_cancel ? true : !progress_fn(std::move(progress)));
                if (cancel)
                {
                    // Lets keep error_message empty here - if there is need to show error dialog, the message will be probably shown by whatever caused the cancel.
                    //error_message = DSKY::format(_u8L("Error getting: `%1%`: Download was canceled."), url);
                    BOOST_LOG_TRIVIAL(debug) << "AppUpdater::priv::http_get_file message: " << error_message;
                }
            })
        .on_error(
            [&](std::string body, std::string error, unsigned http_status)
            {
                error_message = DSKY::format("Error getting: `%1%`: HTTP %2%, %3%", url, http_status, error);
                BOOST_LOG_TRIVIAL(error) << error_message;
            })
        .on_complete(
            [&](std::string body, unsigned /* http_status */)
            {
                assert(complete_fn != nullptr);
                res = complete_fn(body, error_message);
            })
        .perform_sync();

    return res;
}

boost::filesystem::path AppUpdater::priv::download_file(const DownloadAppData &data) const
{
    boost::filesystem::path dest_path;
    size_t last_gui_progress = 0;
    size_t expected_size = data.size;
    dest_path = data.target_path;
    assert(!dest_path.empty());
    if (dest_path.empty())
    {
        std::string line1 = DSKY::format(_u8L("Internal download error for url %1%:"), data.url);
        std::string line2 = _u8L("Destination path is empty.");
        std::string message = DSKY::format("%1%\n%2%", line1, line2);
        BOOST_LOG_TRIVIAL(error) << message;
        wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED);
        evt->SetString(message);
        if (wxApp::GetInstance() != nullptr)
            DSKY::wxGetApp().QueueEvent(evt);
        return boost::filesystem::path();
    }

    boost::filesystem::path tmp_path = dest_path;
    tmp_path += format(".%1%%2%", std::to_string(DSKY::GLCanvas3D::timestamp_now()), ".download");
    FILE *file;
    file = boost::nowide::fopen(tmp_path.string().c_str(), "wb");
    assert(file != NULL);
    if (file == NULL)
    {
        std::string line1 = DSKY::format(_u8L("Download from %1% couldn't start:"), data.url);
        std::string line2 = DSKY::format(_u8L("Can't create file at %1%"), tmp_path.string());
        std::string message = DSKY::format("%1%\n%2%", line1, line2);
        BOOST_LOG_TRIVIAL(error) << message;
        if (wxApp::GetInstance() != nullptr)
        {
            wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED);
            evt->SetString(message);
            DSKY::wxGetApp().QueueEvent(evt);
        }
        return boost::filesystem::path();
    }

    std::string error_message;
    bool res = http_get_file(
        data.url,
        256 * 1024 * 1024
        // on_progress
        ,
        [&last_gui_progress, expected_size](Http::Progress progress)
        {
            // size check (allow 1 byte tolerance for rounding differences)
            if (progress.dltotal > 0 && progress.dltotal > expected_size + 1)
            {
                std::string message = DSKY::format(
                    "Downloading new %1% has failed. The file has incorrect file size. Aborting download.\nExpected size: %2%\nDownload size: %3%",
                    PREFLIGHT_APP_NAME, expected_size, progress.dltotal);
                BOOST_LOG_TRIVIAL(error) << message;
                if (wxApp::GetInstance() != nullptr)
                {
                    wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED);
                    evt->SetString(message);
                    DSKY::wxGetApp().QueueEvent(evt);
                }
                return false;
            }
            else if (progress.dltotal > 0 && progress.dltotal < expected_size)
            {
                // This is possible error, but we cannot know until the download is finished. Somehow the total size can grow during the download.
                BOOST_LOG_TRIVIAL(info) << DSKY::format(
                    "Downloading new %1% has incorrect size. The download will continue. \nExpected size: %2%\nDownload size: %3%",
                    PREFLIGHT_APP_NAME, expected_size, progress.dltotal);
            }
            // progress event
            size_t gui_progress = progress.dltotal > 0 ? 100 * progress.dlnow / progress.dltotal : 0;
            BOOST_LOG_TRIVIAL(debug) << "App download " << gui_progress << "% " << progress.dlnow << " of "
                                     << progress.dltotal;
            if (last_gui_progress < gui_progress && (last_gui_progress != 0 || gui_progress != 100))
            {
                last_gui_progress = gui_progress;
                if (wxApp::GetInstance() != nullptr)
                {
                    wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_APP_DOWNLOAD_PROGRESS);
                    evt->SetString(DSKY::from_u8(std::to_string(gui_progress)));
                    DSKY::wxGetApp().QueueEvent(evt);
                }
            }
            return true;
        }
        // on_complete
        ,
        [&file, dest_path, tmp_path, expected_size](std::string body, std::string &error_message)
        {
            // Size check with 1 byte tolerance for rounding differences
            size_t body_size = body.size();
            if (body_size > expected_size + 1 || (expected_size > 0 && body_size + 1 < expected_size))
            {
                error_message =
                    DSKY::format(_u8L("Downloaded file has wrong size. Expected size: %1% Downloaded size: %2%"),
                                 expected_size, body_size);
                return false;
            }
            if (file == NULL)
            {
                error_message = DSKY::format(_u8L("Can't create file at %1%"), tmp_path.string());
                return false;
            }
            try
            {
                fwrite(body.c_str(), 1, body.size(), file);
                fclose(file);
                boost::filesystem::rename(tmp_path, dest_path);
            }
            catch (const std::exception &e)
            {
                error_message = DSKY::format(_u8L("Failed to write to file or to move %1% to %2%:\n%3%"), tmp_path,
                                             dest_path, e.what());
                return false;
            }
            return true;
        },
        error_message);
    if (!res)
    {
        if (m_cancel)
        {
            BOOST_LOG_TRIVIAL(info) << error_message;
            if (wxApp::GetInstance() != nullptr)
            {
                wxCommandEvent *evt = new wxCommandEvent(
                    EVT_PREFLIGHT_APP_DOWNLOAD_FAILED); // FAILED with empty msg only closes progress notification
                DSKY::wxGetApp().QueueEvent(evt);
            }
        }
        else
        {
            std::string message = (error_message.empty() ? std::string()
                                                         : DSKY::format(_u8L("Downloading new %1% has failed:\n%2%"),
                                                                        PREFLIGHT_APP_NAME, error_message));
            if (wxApp::GetInstance() != nullptr)
            {
                wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED);
                if (!message.empty())
                {
                    BOOST_LOG_TRIVIAL(error) << message;
                    evt->SetString(message);
                }
                DSKY::wxGetApp().QueueEvent(evt);
            }
        }
        return boost::filesystem::path();
    }

    return dest_path;
}

bool AppUpdater::priv::run_downloaded_file(boost::filesystem::path path)
{
    assert(!path.empty());
    return run_file(path);
}

void AppUpdater::priv::version_check(const std::string &version_check_url)
{
    assert(!version_check_url.empty());
    std::string error_message;
    bool res = http_get_file(
        version_check_url,
        5120
        // on_progress
        ,
        [](Http::Progress progress)
        {
            return true;
        }
        // on_complete
        ,
        [&](std::string body, std::string &error_message)
        {
            boost::trim(body);
            parse_version_string(body);
            return true;
        },
        error_message);
    if (!res)
    {
        std::string message = DSKY::format("Downloading %1% version file has failed:\n%2%", PREFLIGHT_APP_NAME,
                                           error_message);
        BOOST_LOG_TRIVIAL(error) << message;
        if (m_triggered_by_user && wxApp::GetInstance() != nullptr)
        {
            wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_APP_DOWNLOAD_FAILED);
            evt->SetString(message);
            DSKY::wxGetApp().QueueEvent(evt);
        }
        return;
    }

    // Fetch release notes (silently skip on failure)
    DownloadAppData app_data = get_app_data();
    if (app_data.version)
    {
        std::string notes_url = DSKY::wxGetApp().app_config->release_notes_url();
        if (!notes_url.empty())
        {
            std::string notes_error;
            std::string notes_body;
            http_get_file(
                notes_url, 32 * 1024, [](Http::Progress) { return true; },
                [&](std::string body, std::string &)
                {
                    boost::trim(body);
                    notes_body = std::move(body);
                    return true;
                },
                notes_error);
            // Only use notes if content looks valid (not a server error page)
            if (!notes_body.empty() && notes_body.substr(0, 9) != "<!DOCTYPE" && notes_body.substr(0, 5) != "<html")
            {
                app_data.release_notes = std::move(notes_body);
                set_app_data(app_data);
            }
        }

        // Send version event to GUI thread
        std::string version = app_data.version.get().to_string();
        BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", PREFLIGHT_APP_NAME,
                                          version);
        if (wxApp::GetInstance() != nullptr)
        {
            wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_VERSION_ONLINE);
            evt->SetString(DSKY::from_u8(version));
            DSKY::wxGetApp().QueueEvent(evt);
        }
    }
}

void AppUpdater::priv::parse_version_string(const std::string &body)
{
    size_t start = body.find('[');
    if (start == std::string::npos)
    {
        BOOST_LOG_TRIVIAL(error)
            << "Could not find property tree in version file. Checking for application update has failed.";
        // Lets send event with current version, this way if user triggered this check, it will notify him about no new version online.
        std::string version = Semver().to_string();
        if (wxApp::GetInstance() != nullptr)
        {
            wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_VERSION_ONLINE);
            evt->SetString(DSKY::from_u8(version));
            DSKY::wxGetApp().QueueEvent(evt);
        }
        return;
    }
    std::string tree_string = body.substr(start);
    boost::property_tree::ptree tree;
    std::stringstream ss(tree_string);
    try
    {
        boost::property_tree::read_ini(ss, tree);
    }
    catch (const boost::property_tree::ini_parser::ini_parser_error &err)
    {
        //throw Luminary::RuntimeError(format("Failed reading version file property tree Error: \"%1%\" at line %2%. \nTree:\n%3%", err.message(), err.line(), tree_string).c_str());
        BOOST_LOG_TRIVIAL(error) << format(
            "Failed reading version file property tree Error: \"%1%\" at line %2%. \nTree:\n%3%", err.message(),
            err.line(), tree_string);
        return;
    }

    DownloadAppData new_data;

    for (const auto &section : tree)
    {
        std::string section_name = section.first;

        // online release version info
        // Determine platform section name
        std::string platform_section;
#ifdef _WIN32
#ifdef _M_ARM64
        platform_section = "release:win-arm64";
#else
        platform_section = "release:win-amd64";
#endif
#elif __APPLE__
        // Apple Silicon uses the default osx section; Intel builds fetch the x86_64 asset
#ifdef __aarch64__
        platform_section = "release:osx";
#else
        platform_section = "release:osx-x64";
#endif
#else
        {
            // Map uname machine to release section (e.g. release:linux-amd64, release:linux-aarch64)
            struct utsname uts;
            if (uname(&uts) == 0)
            {
                std::string arch = uts.machine;
                if (arch == "x86_64")
                    arch = "amd64";
                platform_section = std::string("release:linux-") + arch;
            }
        }
#endif
        if (section_name == platform_section)
        {
            for (const auto &data : section.second)
            {
                if (data.first == "url")
                {
                    new_data.url = data.second.data();
                    new_data.target_path = m_default_dest_folder / AppUpdater::get_filename_from_url(new_data.url);
                    BOOST_LOG_TRIVIAL(info) << format("parsing version string: url: %1%", new_data.url);
                }
                else if (data.first == "size")
                {
                    new_data.size = std::stoi(data.second.data());
                    BOOST_LOG_TRIVIAL(info) << format("parsing version string: expected size: %1%", new_data.size);
                }
                else if (data.first == "action")
                {
                    std::string action = data.second.data();
                    if (action == "browser")
                    {
                        new_data.action = AppUpdaterURLAction::AUUA_OPEN_IN_BROWSER;
                    }
                }
            }
        }

        // Release page URL (from release:url section)
        if (section_name == "release:url")
        {
            for (const auto &data : section.second)
            {
                if (data.first == "url")
                    new_data.release_page = data.second.data();
            }
        }

        // released versions - to be send to UI layer
        if (section_name == "common")
        {
            std::vector<std::string> prerelease_versions;
            for (const auto &data : section.second)
            {
                // release version - save and send to UI layer
                if (data.first == "release")
                {
                    std::string version = data.second.data();
                    boost::optional<Semver> release_version = Semver::parse(version);
                    if (!release_version)
                    {
                        BOOST_LOG_TRIVIAL(error)
                            << format("Received invalid contents from version file: Not a correct semver: `%1%`",
                                      version);
                        return;
                    }
                    new_data.version = release_version;
                    // Send after all data is read
                    /*
					BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", PREFLIGHT_APP_NAME, version);
					wxCommandEvent* evt = new wxCommandEvent(EVT_PREFLIGHT_VERSION_ONLINE);
					evt->SetString(DSKY::from_u8(version));
					DSKY::wxGetApp().QueueEvent(evt);
					*/
                    // prerelease versions - write down to be sorted and send to UI layer
                }
                else if (data.first == "alpha")
                {
                    prerelease_versions.emplace_back(data.second.data());
                }
                else if (data.first == "beta")
                {
                    prerelease_versions.emplace_back(data.second.data());
                }
                else if (data.first == "rc")
                {
                    prerelease_versions.emplace_back(data.second.data());
                }
            }
            // find recent version that is newer than last full release.
            boost::optional<Semver> recent_version;
            std::string version_string;
            for (const std::string &ver_string : prerelease_versions)
            {
                boost::optional<Semver> ver = Semver::parse(ver_string);
                if (ver && *new_data.version < *ver && ((recent_version && *recent_version < *ver) || !recent_version))
                {
                    recent_version = ver;
                    version_string = ver_string;
                }
            }
            // send prerelease version to UI layer
            if (recent_version && wxApp::GetInstance() != nullptr)
            {
                BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...",
                                                  PREFLIGHT_APP_NAME, version_string);
                wxCommandEvent *evt = new wxCommandEvent(EVT_PREFLIGHT_EXPERIMENTAL_VERSION_ONLINE);
                evt->SetString(DSKY::from_u8(version_string));
                DSKY::wxGetApp().QueueEvent(evt);
            }
        }
    }
    assert(!new_data.url.empty());
    assert(new_data.version);
    // save - event is sent from version_check() after optional notes fetch
    set_app_data(new_data);
}

DownloadAppData AppUpdater::priv::get_app_data()
{
    const std::lock_guard<std::mutex> lock(m_data_mutex);
    DownloadAppData ret_val(m_online_version_data);
    return ret_val;
}

void AppUpdater::priv::set_app_data(DownloadAppData data)
{
    const std::lock_guard<std::mutex> lock(m_data_mutex);
    m_online_version_data = data;
}

AppUpdater::AppUpdater() : p(new priv()) {}
AppUpdater::~AppUpdater()
{
    if (p && p->m_thread.joinable())
    {
        // This will stop transfers being done by the thread, if any.
        // Cancelling takes some time, but should complete soon enough.
        p->m_cancel = true;
        p->m_thread.join();
    }
}
void AppUpdater::sync_download()
{
    assert(p);
    // join thread first - it could have been in sync_version
    if (p->m_thread.joinable())
    {
        // This will stop transfers being done by the thread, if any.
        // Cancelling takes some time, but should complete soon enough.
        p->m_cancel = true;
        p->m_thread.join();
    }
    p->m_cancel = false;

    DownloadAppData input_data = p->get_app_data();
    assert(!input_data.url.empty());

    p->m_thread = std::thread(
        [this, input_data]()
        {
            p->m_download_ongoing = true;
            if (boost::filesystem::path dest_path = p->download_file(input_data); boost::filesystem::exists(dest_path))
            {
                if (input_data.start_after)
                {
                    p->run_downloaded_file(std::move(dest_path));
                }
                else
                {
                    DSKY::desktop_open_folder(dest_path.parent_path());
                }
            }
            p->m_download_ongoing = false;
        });
}

void AppUpdater::sync_version(const std::string &version_check_url, bool from_user)
{
    assert(p);
    // join thread first - it could have been in sync_download
    if (p->m_thread.joinable())
    {
        // This will stop transfers being done by the thread, if any.
        // Cancelling takes some time, but should complete soon enough.
        p->m_cancel = true;
        p->m_thread.join();
    }
    p->m_triggered_by_user = from_user;
    p->m_cancel = false;
    p->m_thread = std::thread([this, version_check_url]() { p->version_check(version_check_url); });
}

void AppUpdater::cancel()
{
    p->m_cancel = true;
}
bool AppUpdater::cancel_callback()
{
    cancel();
    return true;
}

std::string AppUpdater::get_default_dest_folder()
{
    return p->m_default_dest_folder.string();
}

std::string AppUpdater::get_filename_from_url(const std::string &url)
{
    size_t slash = url.rfind('/');
    return (slash != std::string::npos ? url.substr(slash + 1) : url);
}

std::string AppUpdater::get_file_extension_from_url(const std::string &url)
{
    size_t dot = url.rfind('.');
    return (dot != std::string::npos ? url.substr(dot) : url);
}

void AppUpdater::set_app_data(DownloadAppData data)
{
    p->set_app_data(std::move(data));
}

DownloadAppData AppUpdater::get_app_data()
{
    return p->get_app_data();
}

bool AppUpdater::get_triggered_by_user() const
{
    return p->get_triggered_by_user();
}

bool AppUpdater::get_download_ongoing() const
{
    return p->get_download_ongoing();
}

} //namespace Luminary
