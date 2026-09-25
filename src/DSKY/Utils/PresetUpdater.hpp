///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <memory>

namespace Luminary
{

// Legacy PresetUpdater shell - profile updates are handled by ProfileServer.
// This class is retained because PresetUpdaterWrapper owns an instance.
class PresetUpdater
{
public:
    PresetUpdater();
    PresetUpdater(PresetUpdater &&) = delete;
    PresetUpdater(const PresetUpdater &) = delete;
    PresetUpdater &operator=(PresetUpdater &&) = delete;
    PresetUpdater &operator=(const PresetUpdater &) = delete;
    ~PresetUpdater();

    // Retained for UpdateDialogs compilation
    enum UpdateResult
    {
        R_NOOP,
        R_INCOMPAT_EXIT,
        R_INCOMPAT_CONFIGURED,
        R_UPDATE_INSTALLED,
        R_UPDATE_REJECT,
        R_UPDATE_NOTIFICATION,
        R_ALL_CANCELED
    };

    enum class UpdateParams
    {
        SHOW_TEXT_BOX,
        SHOW_NOTIFICATION,
        FORCED_BEFORE_WIZARD,
        SHOW_TEXT_BOX_YES_NO
    };

private:
    struct priv;
    std::unique_ptr<priv> p;
};
} // namespace Luminary
