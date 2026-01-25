///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#ifndef slic3r_Format_PrintRequest_hpp_
#define slic3r_Format_PrintRequest_hpp_

namespace Slic3r
{
class Model;
bool load_printRequest(const char *input_file, Model *model);

} //namespace Slic3r

#endif