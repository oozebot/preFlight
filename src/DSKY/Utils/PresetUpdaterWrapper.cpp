///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "PresetUpdaterWrapper.hpp"

#include "DSKY/GUI/GUI_App.hpp"
#include "DSKY/GUI/I18N.hpp"
#include "DSKY/GUI/format.hpp"

using namespace std::chrono;

namespace Luminary
{

wxDEFINE_EVENT(EVT_PRESET_UPDATER_STATUS_END, PresetUpdaterStatusSimpleEvent);
wxDEFINE_EVENT(EVT_PRESET_UPDATER_STATUS_PRINT, PresetUpdaterStatusMessageEvent);
wxDEFINE_EVENT(EVT_CONFIG_UPDATER_SYNC_DONE, wxCommandEvent);
wxDEFINE_EVENT(EVT_CONFIG_UPDATER_FAILED_ARCHIVE, wxCommandEvent);

PresetUpdaterWrapper::PresetUpdaterWrapper()
    : m_preset_archive_database(std::make_unique<PresetArchiveDatabase>())
    , m_preset_updater(std::make_unique<PresetUpdater>())
    , m_ui_status(std::make_unique<PresetUpdaterUIStatus>())
{
}

PresetUpdaterWrapper::~PresetUpdaterWrapper() {}

const std::map<PresetUpdaterUIStatus::PresetUpdaterRetryPolicy, HttpRetryOpt> PresetUpdaterUIStatus::policy_map = {
    {PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES, {500ms, 5s, 4}},
    {PresetUpdaterUIStatus::PresetUpdaterRetryPolicy::PURP_NO_RETRY, {0ms}}};

PresetUpdaterUIStatus::PresetUpdaterUIStatus() {}

void PresetUpdaterUIStatus::reset(PresetUpdaterUIStatus::PresetUpdaterRetryPolicy policy)
{
    if (auto it = policy_map.find(policy); it != policy_map.end())
    {
        m_retry_policy = it->second;
    }
    else
    {
        m_retry_policy = {0ms};
    }

    m_canceled = false;
    m_evt_handler = nullptr;
    m_error_msg.clear();
    m_target.clear();
    m_failed_archives.clear();
}

bool PresetUpdaterUIStatus::on_attempt(int attempt, unsigned delay)
{
    if (attempt == 1)
    {
        set_status(DSKY::format_wxstr(_L("Downloading Resources: %1%"), m_target));
    }
    else
    {
        set_status(
            DSKY::format_wxstr(_L("Downloading Resources: %1%. Attempt %2%."), m_target, std::to_string(attempt)));
    }
    return get_canceled();
}

void PresetUpdaterUIStatus::set_target(const std::string &target)
{
    m_target = target;
}

void PresetUpdaterUIStatus::set_status(const wxString &status)
{
    if (m_evt_handler)
        wxQueueEvent(m_evt_handler, new PresetUpdaterStatusMessageEvent(EVT_PRESET_UPDATER_STATUS_PRINT, status));
}

void PresetUpdaterUIStatus::end()
{
    if (m_evt_handler)
        wxQueueEvent(m_evt_handler, new PresetUpdaterStatusSimpleEvent(EVT_PRESET_UPDATER_STATUS_END));
}

} // namespace Luminary
