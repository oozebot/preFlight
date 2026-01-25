///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "I18N.hpp"

namespace Slic3r
{
namespace GUI
{

wxString L_str(const std::string &str)
{
    //! Explicitly specify that the source string is already in UTF-8 encoding
    return wxGetTranslation(wxString(str.c_str(), wxConvUTF8));
}

} // namespace GUI
} // namespace Slic3r
