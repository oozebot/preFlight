///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <boost/property_tree/ptree_fwd.hpp>

namespace Luminary
{

// The deepest nesting of JSON objects and arrays in a text; brackets inside strings do not count
size_t json_nesting_depth(std::string_view text);

// Parses a JSON reply from a network peer into a property tree. Boost's parser recurses once per
// nesting level with no limit, so a deeply nested reply would overflow the stack; a text nested
// deeper than max_depth is refused with the parser's own exception instead.
void read_json_bounded(const std::string &text, boost::property_tree::ptree &tree, size_t max_depth = 64);

} // namespace Luminary
