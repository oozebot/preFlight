///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2017 - 2020 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <exception>
namespace Slic3r
{

class ConfigError : public Slic3r::RuntimeError
{
    using Slic3r::RuntimeError::RuntimeError;
};

namespace GUI
{

class ConfigGUITypeError : public ConfigError
{
    using ConfigError::ConfigError;
};

} // namespace GUI
} // namespace Slic3r
