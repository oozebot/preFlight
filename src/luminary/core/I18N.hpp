///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <string>

#ifdef PREFLIGHT_CURRENTLY_COMPILING_GUI_MODULE
#ifndef PREFLIGHT_ALLOW_ENGINE_I18N_IN_DSKY
#error The engine I18N.hpp was included into a file belonging to the DSKY module.
#endif
#endif

namespace Luminary
{

namespace I18N
{
typedef std::string (*translate_fn_type)(const char *);
extern translate_fn_type translate_fn;
inline void set_translate_callback(translate_fn_type fn)
{
    translate_fn = fn;
}
inline std::string translate(const std::string &s)
{
    return (translate_fn == nullptr) ? s : (*translate_fn)(s.c_str());
}
inline std::string translate(const char *ptr)
{
    return (translate_fn == nullptr) ? std::string(ptr) : (*translate_fn)(ptr);
}
} // namespace I18N

} // namespace Luminary

// When this is included from the DSKY module, do not define the translation functions.
// Macros from DSKY/GUI/I18N.hpp should be used there.
#ifndef PREFLIGHT_CURRENTLY_COMPILING_GUI_MODULE
#ifdef L
#error L macro is defined where it shouldn't be. Didn't you include DSKY/GUI/I18N.hpp in the engine by mistake?
#endif
namespace
{
[[maybe_unused]] const char *L(const char *s)
{
    return s;
}
[[maybe_unused]] const char *L_CONTEXT(const char *s, const char *context)
{
    return s;
}
[[maybe_unused]] std::string _u8L(const char *s)
{
    return Luminary::I18N::translate(s);
}
} // namespace
#endif
