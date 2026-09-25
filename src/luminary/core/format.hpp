///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

// Functional wrapper around boost::format.
// One day we may replace this wrapper with C++20 format
// https://en.cppreference.com/w/cpp/utility/format/format
// though C++20 format uses a different template pattern for position independent parameters.
//
// Boost::format works around the missing variadic templates with a % chaining operator.
// The usage of boost::format looks like this:
// (boost::format("template") % arg1 %arg2).str()
// This wrapper allows for a nicer syntax:
// Luminary::format("template", arg1, arg2)
// One can also override Luminary::internal::format::cook() function to convert a Luminary::format() argument to something that
// boost::format may convert to string. The GUI layer provides a cook() overload for its string types.

#include <boost/format.hpp>

namespace Luminary
{

// https://gist.github.com/gchudnov/6a90d51af004d97337ec
namespace internal
{
namespace format
{
// Default "cook" function - just forward.
template<typename T>
inline T &&cook(T &&arg)
{
    return std::forward<T>(arg);
}

// End of the recursive chain.
inline std::string format_recursive(boost::format &message)
{
    return message.str();
}

template<typename TValue, typename... TArgs>
std::string format_recursive(boost::format &message, TValue &&arg, TArgs &&...args)
{
    // Format, possibly convert the argument by the "cook" function.
    message % cook(std::forward<TValue>(arg));
    return format_recursive(message, std::forward<TArgs>(args)...);
}
} // namespace format
}; // namespace internal

template<typename... TArgs>
inline std::string format(const char *fmt, TArgs &&...args)
{
    boost::format message(fmt);
    return internal::format::format_recursive(message, std::forward<TArgs>(args)...);
}

template<typename... TArgs>
inline std::string format(const std::string &fmt, TArgs &&...args)
{
    boost::format message(fmt);
    return internal::format::format_recursive(message, std::forward<TArgs>(args)...);
}

} // namespace Luminary
