///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_JsonUtils_hpp_
#define slic3r_JsonUtils_hpp_

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <string>

namespace Slic3r
{

std::string write_json_with_post_process(const boost::property_tree::ptree &ptree);

} // namespace Slic3r

#endif // slic3r_jsonUtils_hpp_
