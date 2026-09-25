///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "DSKY/GUI/PresetArchiveDatabase.hpp"
#include "DSKY/GUI/Event.hpp"
#include "DSKY/Utils/PresetUpdater.hpp"
#include "DSKY/Utils/Http.hpp"

#include <wx/event.h>

#include <memory>
#include <atomic>
#include <map>

namespace Luminary
{
using PresetUpdaterStatusSimpleEvent = DSKY::SimpleEvent;
using PresetUpdaterStatusMessageEvent = DSKY::Event<wxString>;
wxDECLARE_EVENT(EVT_PRESET_UPDATER_STATUS_END, PresetUpdaterStatusSimpleEvent);
wxDECLARE_EVENT(EVT_PRESET_UPDATER_STATUS_PRINT, PresetUpdaterStatusMessageEvent);
wxDECLARE_EVENT(EVT_CONFIG_UPDATER_SYNC_DONE, wxCommandEvent);
wxDECLARE_EVENT(EVT_CONFIG_UPDATER_FAILED_ARCHIVE, wxCommandEvent);

class PresetBundle;
class Semver;

// Status object passed to archive repository operations.
// Retained because PresetArchiveDatabase references it as a parameter type.
class PresetUpdaterUIStatus
{
public:
    enum class PresetUpdaterRetryPolicy
    {
        PURP_5_TRIES,
        PURP_NO_RETRY,
    };
    PresetUpdaterUIStatus();
    ~PresetUpdaterUIStatus() {}
    void set_handler(wxEvtHandler *evt_handler) { m_evt_handler = evt_handler; }

    void reset(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy policy);

    bool on_attempt(int attempt, unsigned delay);
    void set_target(const std::string &target);
    void set_status(const wxString &status);
    void end();
    bool get_canceled() const { return m_canceled.load(); }
    HttpRetryOpt get_retry_policy() const { return m_retry_policy; }
    std::string get_error() const { return m_error_msg; }
    std::string get_target() const { return m_target; }
    void add_failed_archive(const std::string &id) { m_failed_archives.emplace_back(id); }
    const std::vector<std::string> &get_failed_archives() { return m_failed_archives; }

    void set_canceled(bool val) { m_canceled.store(val); }
    void set_error(const std::string &msg) { m_error_msg = msg; }

private:
    wxEvtHandler *m_evt_handler{nullptr};
    std::atomic<bool> m_canceled{false};
    std::string m_error_msg;

    std::string m_target;

    HttpRetryOpt m_retry_policy;
    static const std::map<PresetUpdaterUIStatus::PresetUpdaterRetryPolicy, HttpRetryOpt> policy_map;

    std::vector<std::string> m_failed_archives;
};

// Wrapper providing access to PresetArchiveDatabase.
// The legacy PresetUpdater sync/update system has been removed -
// profile downloads are handled by ProfileServer.
class PresetUpdaterWrapper
{
public:
    PresetUpdaterWrapper();
    ~PresetUpdaterWrapper();

    // PresetArchiveDatabase accessors used by ConfigWizard and UpdatesUIManager
    bool is_selected_repository_by_id(const std::string &repo_id) const
    {
        return m_preset_archive_database->is_selected_repository_by_id(repo_id);
    }
    bool is_selected_repository_by_uuid(const std::string &uuid) const
    {
        return m_preset_archive_database->is_selected_repository_by_uuid(uuid);
    }
    SharedArchiveRepositoryVector get_all_archive_repositories() const
    {
        return m_preset_archive_database->get_all_archive_repositories();
    }
    SharedArchiveRepositoryVector get_selected_archive_repositories() const
    {
        return m_preset_archive_database->get_selected_archive_repositories();
    }
    const std::map<std::string, bool> &get_selected_repositories_uuid() const
    {
        return m_preset_archive_database->get_selected_repositories_uuid();
    }
    bool set_selected_repositories(const std::vector<std::string> &used_uuids, std::string &msg)
    {
        return m_preset_archive_database->set_selected_repositories(used_uuids, msg);
    }
    void set_installed_printer_repositories(const std::vector<std::string> &used_ids)
    {
        m_preset_archive_database->set_installed_printer_repositories(used_ids);
    }
    void remove_local_archive(const std::string &uuid) { m_preset_archive_database->remove_local_archive(uuid); }
    std::string add_local_archive(const boost::filesystem::path path, std::string &msg)
    {
        return m_preset_archive_database->add_local_archive(path, msg);
    }

private:
    std::unique_ptr<PresetArchiveDatabase> m_preset_archive_database;
    std::unique_ptr<PresetUpdater> m_preset_updater;
    std::unique_ptr<PresetUpdaterUIStatus> m_ui_status;
};
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

class PresetUpdaterUIStatusCancel
{
public:
    PresetUpdaterUIStatusCancel(PresetUpdaterUIStatus *ui_status) : p_ui_status(ui_status) {}
    ~PresetUpdaterUIStatusCancel() {}
    void set_cancel(bool c) { p_ui_status->set_canceled(c); }

private:
    PresetUpdaterUIStatus *p_ui_status;
};

} // namespace DSKY
