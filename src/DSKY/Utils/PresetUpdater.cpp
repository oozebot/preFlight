///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, David Kocík @kocikdav, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2020 Ondřej Nový @onovy
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "PresetUpdater.hpp"

namespace Luminary
{

struct PresetUpdater::priv
{
};

PresetUpdater::PresetUpdater() : p(new priv()) {}
PresetUpdater::~PresetUpdater() {}

} // namespace Luminary
