///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "Json.hpp"

#include <algorithm>
#include <sstream>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "luminary/core/diagnostics/DebugCounters.hpp"

namespace Luminary
{

size_t json_nesting_depth(std::string_view text)
{
    size_t depth = 0;
    size_t deepest = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char c : text)
    {
        if (in_string)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                in_string = false;
        }
        else if (c == '"')
            in_string = true;
        else if (c == '{' || c == '[')
            deepest = std::max(deepest, ++depth);
        else if ((c == '}' || c == ']') && depth > 0)
            --depth;
    }
    return deepest;
}

void read_json_bounded(const std::string &text, boost::property_tree::ptree &tree, size_t max_depth)
{
    if (json_nesting_depth(text) > max_depth)
    {
        DBG_COUNT_LOAD("HOST_REPLY_TOO_DEEP");
        throw boost::property_tree::json_parser_error("JSON nested deeper than " + std::to_string(max_depth) +
                                                          " levels",
                                                      std::string(), 0);
    }
    std::istringstream stream(text);
    boost::property_tree::read_json(stream, tree);
}

} // namespace Luminary
