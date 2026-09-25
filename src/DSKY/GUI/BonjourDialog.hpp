///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2022 Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, David Kocík @kocikdav, Vojtěch Král @vojtechkral, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <boost/asio/ip/address.hpp>

#include <wx/dialog.h>
#include <wx/string.h>

#include "luminary/config/catalog/PrintConfig.hpp"

class wxListView;
class wxStaticText;
class wxTimer;
class wxTimerEvent;
class address;

namespace Luminary
{

class Bonjour;
class BonjourReplyEvent;
class ReplySet;

class BonjourDialog : public wxDialog
{
public:
    // service is the mDNS service name queried, without the _ prefix and the ._tcp suffix
    BonjourDialog(wxWindow *parent, Luminary::PrinterTechnology, std::string service = "octoprint");
    BonjourDialog(BonjourDialog &&) = delete;
    BonjourDialog(const BonjourDialog &) = delete;
    BonjourDialog &operator=(BonjourDialog &&) = delete;
    BonjourDialog &operator=(const BonjourDialog &) = delete;
    ~BonjourDialog();

    bool show_and_lookup();
    wxString get_selected() const;

private:
    wxListView *list;
    std::unique_ptr<ReplySet> replies;
    wxStaticText *label;
    std::shared_ptr<Bonjour> bonjour;
    std::unique_ptr<wxTimer> timer;
    unsigned timer_state;
    Luminary::PrinterTechnology tech;
    std::string service;

    virtual void on_reply(BonjourReplyEvent &);
    void on_timer(wxTimerEvent &);
    void on_timer_process();
};

class IPListDialog : public wxDialog
{
public:
    IPListDialog(wxWindow *parent, const wxString &hostname, const std::vector<boost::asio::ip::address> &ips,
                 size_t &selected_index);
    IPListDialog(IPListDialog &&) = delete;
    IPListDialog(const IPListDialog &) = delete;
    IPListDialog &operator=(IPListDialog &&) = delete;
    IPListDialog &operator=(const IPListDialog &) = delete;
    ~IPListDialog();

    virtual void EndModal(int retCode) wxOVERRIDE;

private:
    wxListView *m_list;
    size_t &m_selected_index;
};

} // namespace Luminary
